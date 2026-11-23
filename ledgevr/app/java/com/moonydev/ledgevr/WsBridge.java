package com.moonydev.ledgevr;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.net.URI;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Random;
import java.util.concurrent.atomic.AtomicBoolean;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import java.security.cert.X509Certificate;

/**
 * LEDGE VR WebSocket bridge — thin Java transport for the Photon gateway
 * (wss://…:19093, subprotocol GpBinaryV18). The whole Photon protocol lives in
 * libv40shell.so; this class only speaks TLS + RFC6455 WebSocket framing.
 */
public class WsBridge {
    private SSLSocket socket;
    private InputStream in;
    private OutputStream out;
    private final AtomicBoolean running = new AtomicBoolean(false);
    private Thread reader;

    // called into native
    public static native void nativeOnFrame(byte[] data, int len);
    public static native void nativeOnClosed(String reason);

    public boolean connect(String url) {
        try {
            URI u = new URI(url);
            String host = u.getHost();
            int port = u.getPort() > 0 ? u.getPort() : 443;
            String path = (u.getRawPath() == null ? "/" : u.getRawPath())
                    + (u.getRawQuery() != null ? "?" + u.getRawQuery() : "");

            // trust all: photon gateway certs chain fine normally, but some
            // devices have odd CA stores; certificate pinning is not needed
            TrustManager[] tm = new TrustManager[]{ new X509TrustManager() {
                public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
                public void checkClientTrusted(X509Certificate[] c, String a) {}
                public void checkServerTrusted(X509Certificate[] c, String a) {}
            }};
            SSLContext sc = SSLContext.getInstance("TLS");
            sc.init(null, tm, new SecureRandom());
            SSLSocketFactory f = sc.getSocketFactory();

            Socket plain = new Socket(host, port);
            plain.setTcpNoDelay(true);
            socket = (SSLSocket) f.createSocket(plain, host, port, true);
            socket.setUseClientMode(true);
            socket.startHandshake();

            in = socket.getInputStream();
            out = socket.getOutputStream();

            // --- RFC6455 opening handshake ---
            byte[] keyBytes = new byte[16];
            new SecureRandom().nextBytes(keyBytes);
            String key = android.util.Base64.encodeToString(keyBytes, android.util.Base64.NO_WRAP);

            StringBuilder req = new StringBuilder();
            req.append("GET ").append(path).append(" HTTP/1.1\r\n");
            req.append("Host: ").append(host).append(":").append(port).append("\r\n");
            req.append("Upgrade: websocket\r\n");
            req.append("Connection: Upgrade\r\n");
            req.append("Sec-WebSocket-Key: ").append(key).append("\r\n");
            req.append("Sec-WebSocket-Version: 13\r\n");
            req.append("Sec-WebSocket-Protocol: GpBinaryV18\r\n");
            req.append("User-Agent: LEDGE-VR/1.0\r\n\r\n");
            out.write(req.toString().getBytes(StandardCharsets.US_ASCII));
            out.flush();

            // read until \r\n\r\n
            String head = readHttpHead(in);
            if (!head.contains("101")) {
                closeInternal("handshake rejected: " + head.split("\r\n")[0]);
                return false;
            }

            running.set(true);
            reader = new Thread(this::readLoop, "LedgeWsReader");
            reader.setDaemon(true);
            reader.start();
            return true;
        } catch (Exception e) {
            closeInternal("connect error: " + e.getMessage());
            return false;
        }
    }

    private static String readHttpHead(InputStream in) throws Exception {
        StringBuilder sb = new StringBuilder();
        int prev = -1, c;
        while ((c = in.read()) != -1) {
            sb.append((char) c);
            if (prev == '\r' && c == '\n' && sb.length() >= 4
                    && sb.charAt(sb.length() - 4) == '\r' && sb.charAt(sb.length() - 3) == '\n'
                    && sb.charAt(sb.length() - 2) == '\r' && sb.charAt(sb.length() - 1) == '\n')
                break;
            prev = c;
        }
        return sb.toString();
    }

    private void readLoop() {
        try {
            while (running.get()) {
                int b0 = in.read();
                if (b0 == -1) break;
                int b1 = in.read();
                boolean fin = (b0 & 0x80) != 0;
                int opcode = b0 & 0x0F;
                boolean masked = (b1 & 0x80) != 0;
                long len = b1 & 0x7F;
                if (len == 126) {
                    len = ((in.read() & 0xFF) << 8) | (in.read() & 0xFF);
                } else if (len == 127) {
                    long v = 0;
                    for (int i = 0; i < 8; i++) v = (v << 8) | (in.read() & 0xFF);
                    len = v;
                }
                byte[] mask = null;
                if (masked) {
                    mask = new byte[4];
                    readFully(in, mask, 4);
                }
                byte[] payload = new byte[(int) len];
                readFully(in, payload, (int) len);
                if (masked) for (int i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];

                if (opcode == 0x2 || (opcode == 0x0 && payload.length > 0)) {
                    // binary (fragment continuation handled by immediate delivery)
                    if (payload.length > 0) nativeOnFrame(payload, payload.length);
                } else if (opcode == 0x9) { // ping → pong
                    sendFrame(0x0A, payload);
                } else if (opcode == 0x8) { // close
                    break;
                }
                // 0x1 text frames (session handshake) ignored in GpBinaryV18 mode
            }
        } catch (Exception ignored) {
        }
        closeInternal("closed");
    }

    private static void readFully(InputStream in, byte[] buf, int n) throws Exception {
        int off = 0;
        while (off < n) {
            int r = in.read(buf, off, n - off);
            if (r == -1) throw new java.io.EOFException();
            off += r;
        }
    }

    private synchronized void sendFrame(int opcode, byte[] data) throws Exception {
        if (out == null || !running.get()) return;
        byte[] mask = new byte[4];
        new Random().nextBytes(mask);
        int len = data.length;
        java.io.ByteArrayOutputStream head = new java.io.ByteArrayOutputStream();
        head.write(0x80 | opcode);
        if (len < 126) {
            head.write(0x80 | len);
        } else if (len < 65536) {
            head.write(0x80 | 126);
            head.write((len >> 8) & 0xFF);
            head.write(len & 0xFF);
        } else {
            head.write(0x80 | 127);
            for (int s = 7; s >= 0; s--) head.write((int)((len >> (s * 8)) & 0xFF));
        }
        head.write(mask, 0, 4);
        byte[] masked = new byte[len];
        for (int i = 0; i < len; i++) masked[i] = (byte)(data[i] ^ mask[i & 3]);
        out.write(head.toByteArray());
        out.write(masked);
        out.flush();
    }

    public void send(byte[] data) {
        try {
            sendFrame(0x2, data);
        } catch (Exception ignored) {
        }
    }

    private void closeInternal(String reason) {
        boolean was = running.getAndSet(false);
        try {
            if (socket != null) socket.close();
        } catch (Exception ignored) {
        }
        socket = null;
        in = null;
        out = null;
        if (was) {
            try {
                nativeOnClosed(reason);
            } catch (Throwable ignored) {
            }
        }
    }

    public void close() {
        try {
            if (out != null && running.get()) sendFrame(0x8, new byte[0]);
        } catch (Exception ignored) {
        }
        closeInternal("client close");
    }
}
