// Live Photon custom-auth test against the real master server using a REAL
// PlayFab token from the Photon addon (GetPhotonAuthenticationToken).
// Usage: node photon_auth_test.mjs <variant>
//   1 = keys 226(authType=3)/227(query params)  [official-doc format guess A]
//   2 = keys 226(authType=2)/227(query params)  [official-doc format guess B]
//   3 = keys 217(authType=2)/214(JSON postdata) [current APK format]
//   4 = keys 226(authType=3)/228(JSON postdata) [post-data variant]
import WebSocket from '/home/z/my-project/toolchain/node_modules/ws/index.js';
import fs from 'fs';

const APP_ID = '15f80a97-41e4-4c8b-a887-9b8de6d103ad';
const REGION = 'eu';
const VARIANT = parseInt(process.argv[2] || '1', 10);
const { pfid, token } = JSON.parse(fs.readFileSync('/tmp/pf_creds.json', 'utf8'));

const vint = (n) => { const out = []; do { let b = n & 0x7F; n >>= 7; if (n) b |= 0x80; out.push(b); } while (n); return Buffer.from(out); };
const pstr = (s) => Buffer.concat([vint(s.length), Buffer.from(s, 'utf8')]);
const STR = 0x07, BYTE = 0x02, GPBYTE = 0x03;

function opReq(opCode, params) {
  const out = [0xf3, 0x02, opCode, params.length];
  for (const [key, type, value] of params) {
    out.push(key, type);
    if (type === STR) out.push(...pstr(value));
    else if (type === BYTE || type === GPBYTE) out.push(value & 0xFF);
    else out.push(...value); // raw pre-encoded value bytes (e.g. dict 0x22)
  }
  return Buffer.from(out);
}

const authParams = `username=${pfid}&token=${token}`;
const authJson = JSON.stringify({ username: pfid, token });
const variants = {
  1: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [226, BYTE, 3], [227, STR, authParams]],
  2: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [226, BYTE, 2], [227, STR, authParams]],
  3: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [217, BYTE, 2], [214, STR, authJson]],
  4: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [226, BYTE, 3], [228, STR, authJson]],
  5: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [217, BYTE, 1], [214, STR, authJson]],
  6: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [217, BYTE, 5], [214, STR, authJson]],
  7: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [217, BYTE, 1], [214, STR, authParams]],
  8: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [217, BYTE, 0], [214, STR, authParams]],
  9: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [217, BYTE, 0], [214, STR, authJson]],
  10: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [217, BYTE, 255], [214, STR, authJson]],
  11: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [217, BYTE, 0], [216, STR, authParams]],
  12: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid],
      [217, 0x22, [0xd8, 0x07, authParams.length & 0x7f, ...Buffer.from(authParams)]]],
  13: [[224, STR, APP_ID], [220, STR, '1.0'], [210, STR, REGION],
      [225, STR, pfid], [217, GPBYTE, 0], [216, STR, authParams]],
};

let sawRegions = false;
const ws = new WebSocket(
  `wss://ns.photonengine.io:19093/?libversion=6.0.2.0&sid=${Math.floor(Math.random() * 100)}&app=${APP_ID.replace(/-/g, '')}`,
  ['GpBinaryV18']);

function show(tag, b) {
  const ascii = [...b].map(c => (c >= 32 && c < 127) ? String.fromCharCode(c) : '.').join('');
  console.log(tag, b.length + 'B', ascii);
}

ws.on('open', () => console.log('variant', VARIANT, 'WS open | pfid', pfid, '| token len', token.length));
ws.on('message', (d) => {
  const b = Buffer.from(d);
  if (b[0] === 0xf3 && b[1] === 0x01) {
    ws.send(opReq(220, [[224, STR, APP_ID]]));
  } else if (b[0] === 0xf3 && b[1] === 0x03 && b[2] === 220) {
    if (!sawRegions) { sawRegions = true; show('REGIONS', b); }
    ws.send(opReq(230, variants[VARIANT]));
    console.log('>> Authenticate sent');
  } else if (b[0] === 0xf3 && b[1] === 0x03 && b[2] === 230) {
    show('AUTH-RESPONSE', b);
    // protocol18 response: f3 03 op ... returnCode(int16 BE) somewhere early; scan
    for (let i = 3; i < Math.min(10, b.length - 1); i++) {
      const rc = b.readUInt16BE(i);
      if (rc === 0 || (rc > 2000 && rc < 4000)) { console.log('>> returnCode candidate @' + i + ':', rc); break; }
    }
  } else {
    show('OTHER', b);
  }
});
ws.on('error', (e) => console.log('WS error:', e.message));
ws.on('close', (c, r) => console.log('WS close', c, r.toString()));
setTimeout(() => process.exit(0), 12000);
