import test from 'node:test';
import assert from 'node:assert/strict';

import { H264Depacketizer, avcCodecString, splitBatch } from '../public/js/activity/rtpH264.js';

function rtp(seq, timestamp, payload, marker = false) {
  const p = new Uint8Array(12 + payload.length);
  p[0] = 0x80;
  p[1] = (marker ? 0x80 : 0) | 96;
  p[2] = seq >> 8; p[3] = seq & 0xff;
  new DataView(p.buffer).setUint32(4, timestamp);
  p.set(payload, 12);
  return p;
}

const SPS = [0x67, 0x42, 0xe0, 0x34, 0xaa];
const PPS = [0x68, 0xce];
const stapA = nals => nals.flatMap(n => [n.length >> 8, n.length & 0xff, ...n]);
const annexB = nals => Uint8Array.from(nals.flatMap(n => [0, 0, 0, 1, ...n]));

function collect() {
  const frames = [], losses = [];
  const d = new H264Depacketizer({ onFrame: f => frames.push(f), onLoss: () => losses.push(1) });
  return { d, frames, losses };
}

test('splitBatch separa registros [tipo][tamanho][pacote]', () => {
  const buf = Uint8Array.from([0, 0, 2, 9, 9, 1, 0, 1, 7, 0, 0, 5]); // último incompleto
  const out = [...splitBatch(buf.buffer)].map(r => [r.kind, [...r.packet]]);
  assert.deepEqual(out, [[0, [9, 9]], [1, [7]]]);
});

test('keyframe em STAP-A + FU-A vira um access unit Annex B', () => {
  const { d, frames } = collect();
  const idr = [0x65, 1, 2, 3, 4, 5];
  d.push(rtp(10, 900, [0x78, ...stapA([SPS, PPS])]));
  d.push(rtp(11, 900, [0x7c, 0x80 | 5, ...idr.slice(1, 3)]));
  d.push(rtp(12, 900, [0x7c, 0x40 | 5, ...idr.slice(3)], true));
  assert.equal(frames.length, 1);
  assert.equal(frames[0].key, true);
  assert.deepEqual([...frames[0].data], [...annexB([SPS, PPS, idr])]);
  assert.equal(avcCodecString(frames[0].sps), 'avc1.42e034');
});

test('delta antes do primeiro keyframe é descartado', () => {
  const { d, frames } = collect();
  d.push(rtp(1, 100, [0x41, 1], true));
  assert.equal(frames.length, 0);
  d.push(rtp(2, 200, [0x78, ...stapA([SPS, PPS, [0x65, 9]])], true));
  d.push(rtp(3, 300, [0x41, 2], true));
  assert.deepEqual(frames.map(f => f.key), [true, false]);
});

test('perda de pacote pede keyframe e descarta deltas até ele', () => {
  const { d, frames, losses } = collect();
  d.push(rtp(1, 100, [0x65, 1], true));
  d.push(rtp(3, 200, [0x41, 2], true)); // seq 2 perdido
  d.push(rtp(4, 300, [0x41, 3], true));
  assert.equal(losses.length, 1);
  assert.equal(frames.length, 1);
  d.push(rtp(5, 400, [0x65, 4], true));
  assert.deepEqual(frames.map(f => f.key), [true, true]);
});

test('seq com volta em 65535 continua sem perda', () => {
  const { d, frames, losses } = collect();
  d.push(rtp(65535, 100, [0x65, 1], true));
  d.push(rtp(0, 200, [0x41, 2], true));
  assert.equal(losses.length, 0);
  assert.equal(frames.length, 2);
});

test('pacote só de padding consome seq sem contar como perda', () => {
  const { d, frames, losses } = collect();
  d.push(rtp(1, 100, [0x65, 1], true));
  const padding = rtp(2, 100, [0, 0, 0, 4]);
  padding[0] |= 0x20;
  d.push(padding);
  d.push(rtp(3, 200, [0x41, 2], true));
  assert.equal(losses.length, 0);
  assert.equal(frames.length, 2);
});
