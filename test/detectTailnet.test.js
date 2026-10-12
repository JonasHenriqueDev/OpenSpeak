import test from 'node:test';
import assert from 'node:assert/strict';
import { detectTailnetAddress } from '../public/js/core/detectTailnet.js';

function probe(candidates, fail = false) {
  return class extends EventTarget {
    static closed = false;
    constructor(config) { super(); assert.deepEqual(config.iceServers, []); }
    createDataChannel() {}
    async createOffer() { if (fail) throw Error('blocked'); return {}; }
    async setLocalDescription() {
      for (const address of [...candidates, null]) {
        const event = new Event('icecandidate');
        event.candidate = address ? { address } : null;
        this.dispatchEvent(event);
      }
    }
    close() { this.constructor.closed = true; }
  };
}

test('detects the exposed Tailscale interface and closes its probe', async () => {
  const PeerConnection = probe(['192.168.1.2', '100.94.54.119']);
  assert.equal(await detectTailnetAddress({ PeerConnection }), '100.94.54.119');
  assert.ok(PeerConnection.closed);
});
test('masked addresses and unavailable WebRTC fall back to manual entry', async () => {
  assert.equal(await detectTailnetAddress({ PeerConnection: probe(['masked.local']) }), null);
  assert.equal(await detectTailnetAddress({ PeerConnection: null }), null);
  const PeerConnection = probe([], true);
  assert.equal(await detectTailnetAddress({ PeerConnection }), null);
  assert.ok(PeerConnection.closed);
});
test('probe timeout closes the connection without requesting media', async () => {
  const PeerConnection = probe([]);
  PeerConnection.prototype.setLocalDescription = async () => {};
  assert.equal(await detectTailnetAddress({ PeerConnection, timeoutMs: 5 }), null);
  assert.ok(PeerConnection.closed);
});
