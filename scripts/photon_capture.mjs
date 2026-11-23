// Capture the official Photon web client's wire traffic via WebSocket interception.
// Ground truth for LEDGE VR's C++ protocol implementation.
import { createRealtimeClient } from '/home/z/my-project/toolchain/ref_photonjs/package/dist/index.js';

const APP_ID = process.argv[2] || '15f80a97-41e4-4c8b-a887-9b8de6d103ad';
const ROOM = process.argv[3] || 'LEDGE-TEST';

const frames = [];
let frameId = 0;
const OrigWS = globalThis.WebSocket;
globalThis.WebSocket = class extends OrigWS {
    constructor(url, protocols) {
        console.log(`[ws] open url=${url}`);
        super(url, protocols);
    }
    send(data) {
        const hex = typeof data === 'string'
            ? Buffer.from(data, 'binary').toString('hex')
            : Buffer.from(data).toString('hex');
        frames.push({ dir: 'SEND', hex });
        console.log(`[ws SEND ${++frameId}] ${hex.length / 2}B ${hex}`);
        super.send(data);
    }
};
// wrap onmessage setter
const origSet = Object.getOwnPropertyDescriptor(OrigWS.prototype, 'onmessage');
Object.defineProperty(OrigWS.prototype, 'onmessage', {
    set(fn) {
        origSet.set.call(this, function (ev) {
            const data = ev.data;
            const hex = typeof data === 'string'
                ? Buffer.from(data, 'binary').toString('hex')
                : Buffer.from(data).toString('hex');
            frames.push({ dir: 'RECV', hex });
            console.log(`[ws RECV ${++frameId}] ${hex.length / 2}B ${hex}`);
            return fn.call(this, ev);
        });
    },
    get() { return origSet.get.call(this); }
});

const client = await createRealtimeClient({ appId: APP_ID, appVersion: '1.0' });
client.on('disconnected', (c, r) => console.log('EVENT disconnected', c, r));
client.on('error', (c, m) => console.log('EVENT error', c, m));
client.on('warning', (c, m) => console.log('EVENT warning', c, m));
client.on('roomJoined', (room, actors) => console.log('EVENT roomJoined', room, actors));
client.on('roomListUpdated', (rooms) => console.log('EVENT roomListUpdated', rooms && rooms.length));

console.log('connecting...');
try {
    await client.connect({ });
    console.log('connected!');
} catch (e) {
    console.log('connect failed:', e.message);
    process.exit(1);
}

setTimeout(async () => {
    console.log('>>> joining room', ROOM);
    try {
        await client.joinRoom(ROOM);
        console.log('room joined');
    } catch (e) {
        console.log('joinRoom failed:', e.message);
    }
    setTimeout(() => {
        console.log('done. frames:', frames.length);
        process.exit(0);
    }, 12000);
}, 8000);
