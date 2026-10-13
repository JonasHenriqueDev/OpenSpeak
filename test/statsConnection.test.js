import test from 'node:test';
import assert from 'node:assert/strict';
import { statsTransmissionMode } from '../public/js/ui/statsConnection.js';
import { StreamMode, RELAY_CLID } from '../public/js/core/protocol.js';

test('statistics distinguish SFU from P2P for publishers and viewers independently of ICE route', () => {
  assert.equal(statsTransmissionMode({ own: { config: { mode: StreamMode.SFU } } }), StreamMode.SFU);
  assert.equal(statsTransmissionMode({ stream: { mode: StreamMode.SFU } }), StreamMode.SFU);
  assert.equal(statsTransmissionMode({ view: { peerClid: RELAY_CLID } }), StreamMode.SFU);
  assert.equal(statsTransmissionMode({ own: { config: { mode: StreamMode.P2P } } }), StreamMode.P2P);
  assert.equal(statsTransmissionMode({ stream: { mode: StreamMode.P2P }, view: { peerClid: 12 } }), StreamMode.P2P);
  assert.equal(statsTransmissionMode({}), null);
});
