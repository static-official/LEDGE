package com.moonydev.ledgevr;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * LEDGE VR — PlayFab HTTPS bridge.
 * Called from native worker threads (never the UI thread). Transfers raw
 * byte[] in and out so Unicode survives without modified-UTF-8 mangling.
 * Returns: ASCII decimal HTTP code, '\n', then the raw response body bytes.
 * On a transport failure the code is 0 and the "body" is the exception text.
 */
public final class HttpBridge {
    private HttpBridge() {}

    public static byte[] post(String url, byte[] bodyBytes, String headersCsv) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) (new URL(url).openConnection());
            c.setRequestMethod("POST");
            c.setConnectTimeout(10000);
            c.setReadTimeout(20000);
            c.setDoOutput(true);
            c.setRequestProperty("Content-Type", "application/json");
            c.setRequestProperty("Accept", "application/json");
            if (headersCsv != null && headersCsv.length() > 0) {
                for (String h : headersCsv.split("\n")) {
                    int colon = h.indexOf(':');
                    if (colon > 0)
                        c.setRequestProperty(h.substring(0, colon), h.substring(colon + 1));
                }
            }
            if (bodyBytes != null && bodyBytes.length > 0) {
                c.setFixedLengthStreamingMode(bodyBytes.length);
                OutputStream os = c.getOutputStream();
                os.write(bodyBytes);
                os.flush();
                os.close();
            }
            int code = c.getResponseCode();
            InputStream in = code >= 400 ? c.getErrorStream() : c.getInputStream();
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            if (in != null) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) bos.write(buf, 0, n);
                in.close();
            }
            byte[] body = bos.toByteArray();
            byte[] prefix = (code + "\n").getBytes(StandardCharsets.UTF_8);
            byte[] out = new byte[prefix.length + body.length];
            System.arraycopy(prefix, 0, out, 0, prefix.length);
            System.arraycopy(body, 0, out, prefix.length, body.length);
            return out;
        } catch (Exception e) {
            String msg = "0\n" + e.toString();
            return msg.getBytes(StandardCharsets.UTF_8);
        } finally {
            if (c != null) c.disconnect();
        }
    }
}
