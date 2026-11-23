// Preload patch: hook the 'ws' module so we can capture Photon native-client wire frames.
const Module = require('module');
const origRequire = Module.prototype.require;
Module.prototype.require = function (id) {
    const w = origRequire.apply(this, arguments);
    if (id === 'ws' && w && w.WebSocket && !w.WebSocket.__patched) {
        w.WebSocket.__patched = true;
        const WS = w.WebSocket;
        const origSend = WS.prototype.send;
        WS.prototype.send = function (data, opts, cb) {
            try {
                const b = Buffer.isBuffer(data) ? data : Buffer.from(data);
                console.log(`[SEND ${b.length}B] ${b.toString('hex')}`);
            } catch (e) {}
            return origSend.call(this, data, opts, cb);
        };
        const origEmit = WS.prototype.emit;
        WS.prototype.emit = function (ev, ...args) {
            if (ev === 'message') {
                try {
                    const d = args[0] && args[0].data;
                    if (d) {
                        const b = Buffer.isBuffer(d) ? d : Buffer.from(d);
                        console.log(`[RECV ${b.length}B] ${b.toString('hex')}`);
                    }
                } catch (e) {}
            }
            return origEmit.call(this, ev, ...args);
        };
        console.log('[patch] ws module hooked');
    }
    return w;
};
