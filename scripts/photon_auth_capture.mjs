// Capture the OFFICIAL Photon client's Authenticate frame when connecting with
// real PlayFab Photon-addon credentials (type=custom, query params).
// Also proves/disproves the dashboard custom-auth URL configuration end-to-end.
import { createRealtimeClient } from '/home/z/my-project/toolchain/node_modules/@photonengine/realtime-core/dist/index.js';
import fs from 'fs';

const APP_ID = '15f80a97-41e4-4c8b-a887-9b8de6d103ad';
const { pfid, token } = JSON.parse(fs.readFileSync('/tmp/pf_creds.json', 'utf8'));

let frameId = 0;
const OrigWS = globalThis.WebSocket;
globalThis.WebSocket = class extends OrigWS {
    send(data) {
        const hex = Buffer.from(data).toString('hex');
        console.log(`[SEND ${++frameId}] ${hex.length / 2}B ${hex}`);
        super.send(data);
    }
};
const origSet = Object.getOwnPropertyDescriptor(OrigWS.prototype, 'onmessage');
Object.defineProperty(OrigWS.prototype, 'onmessage', {
    set(fn) {
        origSet.set.call(this, function (ev) {
            const hex = Buffer.from(ev.data).toString('hex');
            console.log(`[RECV ${++frameId}] ${hex.length / 2}B ${hex}`);
            return fn.call(this, ev);
        });
    },
    get() { return origSet.get.call(this); }
});

const client = await createRealtimeClient({ appId: APP_ID, appVersion: '1.0' });
client.on('error', (c, m) => console.log('EVENT error', c, m));
client.on('warning', (c, m) => console.log('EVENT warning', c, m));

console.log('connecting with custom auth as', pfid, '...');
try {
    await client.connect({
        auth: {
            type: 'custom',
            userId: pfid,
            parameters: `username=${pfid}&token=${token}`,
        },
    });
    console.log('CONNECTED + AUTHENTICATED (official client, PlayFab token)');
} catch (e) {
    console.log('connect failed:', e.message);
}
setTimeout(() => process.exit(0), 8000);
