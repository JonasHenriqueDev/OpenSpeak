import { test } from 'node:test';
import assert from 'node:assert/strict';
import { emptySummary, analyzeIssues, summarizeReport } from '../public/js/video/StatsCollector.js';
import { Severity } from '../public/js/core/protocol.js';

const target = { width: 1280, height: 720, fps: 30 };

function videoSummary(patch = {}) {
  const s = emptySummary('video');
  s.video = { width: 1280, height: 720, fps: 30, encode: { avgEncodeTime: 0.01, limitReason: 'none' } };
  return Object.assign(s, patch);
}

test('sem problemas', () => {
  const s = analyzeIssues(videoSummary(), target, true);
  assert.equal(s.issueStatus, Severity.OK);
  assert.deepEqual(s.issues, []);
});

test('limiares de rede iguais ao TS', () => {
  const minor = analyzeIssues(videoSummary({ fractionLost: 0.01, lastRoundTripTime: 0.4, jitter: 0.2 }), target, true);
  assert.equal(minor.issueSeverity['network:packetloss'], Severity.MINOR);
  assert.equal(minor.issueSeverity['network:rtt'], Severity.MINOR);
  assert.equal(minor.issueSeverity['network:jitter'], Severity.MINOR);

  const major = analyzeIssues(videoSummary({ fractionLost: 0.06, lastRoundTripTime: 0.7 }), target, true);
  assert.equal(major.issueSeverity['network:packetloss'], Severity.MAJOR);
  assert.equal(major.issueSeverity['network:rtt'], Severity.MAJOR);
  assert.ok(major.issueStatus & Severity.MAJOR);

  const s = videoSummary();
  s.sent.currentDelay = 6;
  s.retransmit.bps = 1000;
  analyzeIssues(s, target, true);
  assert.equal(s.issueSeverity['network:packetdelay'], Severity.MINOR);
  assert.equal(s.issueSeverity['network:retransmit'], Severity.MINOR);
});

test('problemas do codificador', () => {
  const s = videoSummary();
  s.video = { width: 640, height: 360, fps: 15, encode: { avgEncodeTime: 0.06, limitReason: 'cpu' } };
  analyzeIssues(s, target, true);
  // 640x360 < 33% de 1280x720 -> grave
  assert.equal(s.issueSeverity['video:qualitylimit'], Severity.MAJOR);
  // 60ms > 1.5 * 33ms -> grave
  assert.equal(s.issueSeverity['video:encodetime'], Severity.MAJOR);
  // 15 < 0.6 * 30 -> grave
  assert.equal(s.issueSeverity['video:fps'], Severity.MAJOR);
  assert.equal(s.issues.find(i => i.name === 'fps').expected, '30');

  const mild = videoSummary();
  mild.video = { width: 1280, height: 720, fps: 23, encode: { avgEncodeTime: 0.04, limitReason: 'bandwidth' } };
  analyzeIssues(mild, target, true);
  assert.equal(mild.issueSeverity['video:qualitylimit'], Severity.MINOR);
  assert.equal(mild.issueSeverity['video:encodetime'], Severity.MINOR);
  assert.equal(mild.issueSeverity['video:fps'], Severity.MINOR);

  // lado de entrada não verifica codificador
  const inbound = videoSummary();
  inbound.video.encode.limitReason = 'cpu';
  analyzeIssues(inbound, target, false);
  assert.equal(inbound.issues.length, 0);
});

test('summarizeReport calcula deltas de saída', () => {
  const mk = (ts, bytes, packets, delay, frames, encTime) => new Map([
    ['o', { id: 'o', type: 'outbound-rtp', kind: 'video', ssrc: 1, timestamp: ts, bytesSent: bytes, packetsSent: packets,
      totalPacketSendDelay: delay, framesEncoded: frames, totalEncodeTime: encTime, frameWidth: 1280, frameHeight: 720,
      framesPerSecond: 30, encoderImplementation: 'libvpx', qualityLimitationReason: 'none', codecId: 'c', targetBitrate: 3e6 }],
    ['c', { id: 'c', type: 'codec', mimeType: 'video/VP9' }],
    ['r', { id: 'r', type: 'remote-inbound-rtp', localId: 'o', ssrc: 1, fractionLost: 0.02, jitter: 0.01, roundTripTime: 0.05,
      totalRoundTripTime: 0.5, roundTripTimeMeasurements: 10, packetsLost: 3 }],
  ]);
  const first = summarizeReport(mk(1000, 0, 0, 0, 0, 0), null, { kind: 'video', isInbound: false });
  const second = summarizeReport(mk(2000, 125_000, 100, 0.5, 30, 0.3), first, { kind: 'video', isInbound: false });
  assert.equal(second.sent.bps, 1_000_000);
  assert.equal(second.sent.currentDelay, 0.005);
  assert.equal(second.video.encode.avgEncodeTime, 0.01);
  assert.equal(second.codec, 'video/VP9');
  assert.equal(second.avgRoundTripTime, 0.05);
  assert.equal(second.fractionLost, 0.02);
});

test('summarizeReport calcula perda de entrada por delta', () => {
  const mk = (ts, recv, lost) => new Map([
    ['i', { id: 'i', type: 'inbound-rtp', kind: 'video', timestamp: ts, packetsReceived: recv, packetsLost: lost, bytesReceived: recv * 1000 }],
  ]);
  const a = summarizeReport(mk(0, 0, 0), null, { kind: 'video', isInbound: true });
  const b = summarizeReport(mk(1000, 90, 10), a, { kind: 'video', isInbound: true });
  assert.equal(b.fractionLost, 0.1);
  assert.equal(b.received.bps, 720_000);
});
