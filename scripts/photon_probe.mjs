// Raw Photon gateway probe (WebSocket subprotocol GpBinaryV18).
// hello -> GetRegions -> auth variants.
import WebSocket from '/home/z/my-project/toolchain/node_modules/ws/index.js';

const APP_ID = (process.argv[2] || '15f80a97-41e4-4c8b-a887-9b8de6d103ad');
const REGION = process.argv[3] || 'eu';
const VARIANT = parseInt(process.argv[4] || '0', 10);
const APP_ID_RAW = APP_ID.replace(/-/g, '');

const vint = (n) => { const out = []; do { let b = n & 0x7F; n >>= 7; if (n) b |= 0x80; out.push(b); } while (n); return Buffer.from(out); };
const pstr = (s) => Buffer.concat([vint(s.length), Buffer.from(s, 'utf8')]);
const STR = 0x07;
const BYTE = 0x02;

function opReq(opCode, params) {
  const out = [0xf3, 0x02, opCode, params.length];
  for (const [key, type, value] of params) {
    out.push(key);
    out.push(type);
    if (type === STR) out.push(...pstr(value));
    else if (type === BYTE) out.push(value & 0xFF);
    else out.push(...value);
  }
  return Buffer.from(out);
}

const authVariant = VARIANT;
const ws = new WebSocket(
  `wss://ns.photonengine.io:19093/?libversion=6.0.2.0&sid=${Math.floor(Math.random() * 100)}&app=${APP_ID_RAW}`,
  ['GpBinaryV18']);

ws.on('open', () => console.log('WS open'));
ws.on('message', (d) => {
  const b = Buffer.from(d);
  console.log('RECV', b.toString('hex'));
  if (b[0] === 0xf3 && b[1] === 0x01) {
    console.log('>> GetRegions');
    ws.send(opReq(220, [[224, STR, APP_ID]]));
  } else if (b[0] === 0xf3 && b[1] === 0x03 && b[2] === 220) {
    const v = authVariant > 0 ? authVariant : 1;
    console.log('>> auth variant', v);
    if (v === 1) {
      ws.send(opReq(230, [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION]]));
    } else if (v === 2) {
      ws.send(opReq(230, [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
        [225, STR, 'ledgevr_user_' + Math.floor(Math.random() * 99999)], [217, BYTE, 0xFF]]));
    } else if (v === 3) {
      ws.send(opReq(230, [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION], [217, BYTE, 0x00]]));
    }
  } else if (b[0] === 0xf3 && b[1] === 0x03 && b[2] === 230) {
    console.log('^^ AUTH RESPONSE');
  }
});
ws.on('error', (e) => console.log('WS error:', e.message));
ws.on('close', (c, r) => console.log('WS close', c, r.toString()));
setTimeout(() => process.exit(0), 15000);
