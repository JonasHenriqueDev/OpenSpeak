// Coleta de estatísticas WebRTC e detecção de problemas.
//
// O resumo e os limiares de `analyzeIssues` reproduzem exatamente o
// comportamento observado no cliente TS 6 (docs/ANALISE_TEAMSPEAK_P2P.md §4).
// As funções puras não dependem do DOM e são testadas em test/stats.test.js.

import { EventEmitter } from '../core/EventEmitter.js';
import { Severity } from '../core/protocol.js';

export const HISTORY_LENGTH = 60;

export function emptySummary(kind) {
  return {
    kind,
    timestamp: 0,
    codec: '',
    packetsLost: 0,
    fractionLost: 0,
    jitter: 0,
    lastRoundTripTime: 0,
    avgRoundTripTime: 0,
    sent: { packets: 0, bytes: 0, bps: 0, currentDelay: 0, totalDelay: 0 },
    received: { packets: 0, bytes: 0, bps: 0 },
    retransmit: { packets: 0, bytes: 0, bps: 0 },
    targetBitrate: 0,
    srtpCipher: '',
    tlsVersion: '',
    connectionType: '',
    video: { width: 0, height: 0, fps: 0 },
    issueStatus: Severity.OK,
    issues: [],
    issueSeverity: {},
    _raw: null,
  };
}

const rate = (cur, prev, seconds) => (seconds > 0 && prev != null && cur != null ? (cur - prev) / seconds : 0);
const ratio = (num, den) => (den > 0 ? num / den : 0);

function indexReport(report) {
  const byId = new Map();
  const byType = new Map();
  report.forEach(stat => {
    byId.set(stat.id, stat);
    if (!byType.has(stat.type)) byType.set(stat.type, []);
    byType.get(stat.type).push(stat);
  });
  return { byId, byType, all: type => byType.get(type) ?? [] };
}

function transportInfo(idx) {
  const transport = idx.all('transport')[0];
  let pair = transport?.selectedCandidatePairId ? idx.byId.get(transport.selectedCandidatePairId) : null;
  if (!pair) pair = idx.all('candidate-pair').find(p => p.nominated && p.state === 'succeeded') ?? null;
  const local = pair ? idx.byId.get(pair.localCandidateId) : null;
  const remote = pair ? idx.byId.get(pair.remoteCandidateId) : null;
  let connectionType = '';
  if (local && remote) {
    connectionType = local.candidateType === 'relay' || remote.candidateType === 'relay'
      ? 'relay'
      : local.candidateType === 'host' && remote.candidateType === 'host' ? 'host' : 'srflx';
  }
  return {
    srtpCipher: transport?.srtpCipher ?? '',
    tlsVersion: transport?.tlsVersion ?? '',
    pair,
    connectionType,
  };
}

/**
 * Constrói o resumo de um tipo de mídia a partir de um RTCStatsReport.
 * @param {Map|RTCStatsReport} report
 * @param {object|null} prev   resumo anterior (para deltas)
 * @param {{kind:'video'|'audio', isInbound:boolean}} o
 */
export function summarizeReport(report, prev, { kind, isInbound }) {
  const idx = indexReport(report);
  const s = emptySummary(kind);
  const t = transportInfo(idx);
  s.srtpCipher = t.srtpCipher;
  s.tlsVersion = t.tlsVersion;
  s.connectionType = t.connectionType;
  const prevRaw = prev?._raw ?? null;

  if (isInbound) {
    const rtp = idx.all('inbound-rtp').find(r => r.kind === kind);
    if (!rtp) return s;
    s.timestamp = rtp.timestamp;
    const dt = prevRaw ? (rtp.timestamp - prevRaw.timestamp) / 1000 : 0;
    s.codec = idx.byId.get(rtp.codecId)?.mimeType ?? '';
    s.received.packets = rtp.packetsReceived ?? 0;
    s.received.bytes = rtp.bytesReceived ?? 0;
    s.received.bps = rate(s.received.bytes, prevRaw?.bytes, dt) * 8;
    s.packetsLost = Math.max(0, rtp.packetsLost ?? 0);
    const dLost = prevRaw ? s.packetsLost - prevRaw.packetsLost : 0;
    const dRecv = prevRaw ? s.received.packets - prevRaw.packets : 0;
    s.fractionLost = Math.max(0, ratio(dLost, dLost + dRecv));
    s.jitter = rtp.jitter ?? 0;
    s.retransmit.packets = rtp.retransmittedPacketsReceived ?? 0;
    s.retransmit.bytes = rtp.retransmittedBytesReceived ?? 0;
    s.retransmit.bps = rate(s.retransmit.bytes, prevRaw?.rtxBytes, dt) * 8;
    if (t.pair) {
      s.lastRoundTripTime = t.pair.currentRoundTripTime ?? 0;
      s.avgRoundTripTime = ratio(t.pair.totalRoundTripTime ?? 0, t.pair.responsesReceived ?? 0);
    }
    if (kind === 'video') {
      s.video.width = rtp.frameWidth ?? 0;
      s.video.height = rtp.frameHeight ?? 0;
      s.video.fps = Math.round(rtp.framesPerSecond ?? 0);
      const dFrames = prevRaw ? (rtp.framesDecoded ?? 0) - prevRaw.frames : 0;
      const dTime = prevRaw ? (rtp.totalDecodeTime ?? 0) - prevRaw.codingTime : 0;
      s.video.decode = {
        decoder: rtp.decoderImplementation ?? '',
        avgDecodeTime: ratio(dTime, dFrames),
        freezeCount: rtp.freezeCount ?? 0,
        powerEfficient: !!rtp.powerEfficientDecoder,
      };
    }
    s._raw = {
      timestamp: rtp.timestamp, bytes: s.received.bytes, packets: s.received.packets,
      packetsLost: s.packetsLost, rtxBytes: s.retransmit.bytes,
      frames: rtp.framesDecoded ?? 0, codingTime: rtp.totalDecodeTime ?? 0,
    };
    return s;
  }

  const rtp = idx.all('outbound-rtp').find(r => r.kind === kind && r.active !== false)
    ?? idx.all('outbound-rtp').find(r => r.kind === kind);
  if (!rtp) return s;
  s.timestamp = rtp.timestamp;
  const dt = prevRaw ? (rtp.timestamp - prevRaw.timestamp) / 1000 : 0;
  s.codec = idx.byId.get(rtp.codecId)?.mimeType ?? '';
  s.sent.packets = rtp.packetsSent ?? 0;
  s.sent.bytes = rtp.bytesSent ?? 0;
  s.sent.bps = rate(s.sent.bytes, prevRaw?.bytes, dt) * 8;
  s.sent.totalDelay = rtp.totalPacketSendDelay ?? 0;
  s.sent.currentDelay = prevRaw
    ? Math.max(0, ratio(s.sent.totalDelay - prevRaw.totalDelay, s.sent.packets - prevRaw.packets))
    : 0;
  s.retransmit.packets = rtp.retransmittedPacketsSent ?? 0;
  s.retransmit.bytes = rtp.retransmittedBytesSent ?? 0;
  s.retransmit.bps = rate(s.retransmit.bytes, prevRaw?.rtxBytes, dt) * 8;
  s.targetBitrate = rtp.targetBitrate ?? 0;

  const remote = idx.all('remote-inbound-rtp').find(r => r.localId === rtp.id || r.ssrc === rtp.ssrc);
  if (remote) {
    s.packetsLost = Math.max(0, remote.packetsLost ?? 0);
    s.fractionLost = Math.max(0, remote.fractionLost ?? 0);
    s.jitter = remote.jitter ?? 0;
    s.lastRoundTripTime = remote.roundTripTime ?? 0;
    s.avgRoundTripTime = ratio(remote.totalRoundTripTime ?? 0, remote.roundTripTimeMeasurements ?? 0);
  } else if (t.pair) {
    s.lastRoundTripTime = t.pair.currentRoundTripTime ?? 0;
    s.avgRoundTripTime = ratio(t.pair.totalRoundTripTime ?? 0, t.pair.responsesReceived ?? 0);
  }

  if (kind === 'video') {
    s.video.width = rtp.frameWidth ?? 0;
    s.video.height = rtp.frameHeight ?? 0;
    s.video.fps = Math.round(rtp.framesPerSecond ?? 0);
    const dFrames = prevRaw ? (rtp.framesEncoded ?? 0) - prevRaw.frames : 0;
    const dTime = prevRaw ? (rtp.totalEncodeTime ?? 0) - prevRaw.codingTime : 0;
    s.video.encode = {
      encoder: rtp.encoderImplementation ?? '',
      avgEncodeTime: ratio(dTime, dFrames),
      limitReason: rtp.qualityLimitationReason ?? 'none',
      scalabilityMode: rtp.scalabilityMode ?? '',
      powerEfficient: !!rtp.powerEfficientEncoder,
    };
  }
  s._raw = {
    timestamp: rtp.timestamp, bytes: s.sent.bytes, packets: s.sent.packets,
    totalDelay: s.sent.totalDelay, rtxBytes: s.retransmit.bytes,
    frames: rtp.framesEncoded ?? 0, codingTime: rtp.totalEncodeTime ?? 0,
  };
  return s;
}

/**
 * Detecta problemas. Mesmos limiares do TS 6.
 * @param {object} s            resumo (é modificado: issues/issueStatus/issueSeverity)
 * @param {{width:number,height:number,fps:number}|null} target  qualidade alvo
 * @param {boolean} checkEncoder  verifica problemas do codificador (lado de saída)
 */
export function analyzeIssues(s, target, checkEncoder) {
  let status = Severity.OK;
  const issues = [];
  const push = (category, name, value, severity, extra = {}) => {
    issues.push({ category, name, value, severity, ...extra });
    status |= severity;
  };

  if (s.fractionLost > 0.001) {
    push('network', 'packetloss', `${+(100 * s.fractionLost).toFixed(2)}%`, s.fractionLost > 0.05 ? Severity.MAJOR : Severity.MINOR);
  }
  if (s.jitter > 0.1) push('network', 'jitter', `${Math.round(1000 * s.jitter)}ms`, Severity.MINOR);
  if (s.lastRoundTripTime > 0.3) {
    push('network', 'rtt', `${Math.round(1000 * s.lastRoundTripTime)}ms`, s.lastRoundTripTime > 0.6 ? Severity.MAJOR : Severity.MINOR);
  }
  if (s.sent.currentDelay > 5) push('network', 'packetdelay', `${Math.round(1000 * s.sent.currentDelay)}ms`, Severity.MINOR);
  if (s.retransmit.bps > 0) push('network', 'retransmit', formatBitrate(s.retransmit.bps), Severity.MINOR);

  if (s.kind === 'video' && checkEncoder && s.video.encode) {
    const enc = s.video.encode;
    if (enc.limitReason && enc.limitReason !== 'none') {
      const major = target && target.width > 0 && s.video.width * s.video.height < target.width * target.height * 0.33;
      push('video', 'qualitylimit', enc.limitReason, major ? Severity.MAJOR : Severity.MINOR);
    }
    if (target && target.fps > 0) {
      const frameTime = 1 / target.fps;
      if (enc.avgEncodeTime > frameTime) {
        push('video', 'encodetime', `${Math.round(1000 * enc.avgEncodeTime)}ms`, enc.avgEncodeTime > 1.5 * frameTime ? Severity.MAJOR : Severity.MINOR);
      }
      if (s.video.fps < 0.8 * target.fps) {
        push('video', 'fps', `${s.video.fps}`, s.video.fps < 0.6 * target.fps ? Severity.MAJOR : Severity.MINOR, { expected: `${target.fps}` });
      }
    }
  }

  s.issueStatus = status;
  s.issues = issues;
  s.issueSeverity = {};
  for (const issue of issues) s.issueSeverity[`${issue.category}:${issue.name}`] = issue.severity;
  return s;
}

export function formatBitrate(bps, digits = 1) {
  if (!Number.isFinite(bps) || bps <= 0) return '0 bps';
  const units = ['bps', 'Kbps', 'Mbps', 'Gbps'];
  let i = 0;
  while (bps >= 1000 && i < units.length - 1) {
    bps /= 1000;
    i++;
  }
  return `${bps.toFixed(i === 0 ? 0 : digits)} ${units[i]}`;
}

export function formatBytes(bytes, digits = 1) {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B';
  const units = ['B', 'KiB', 'MiB', 'GiB'];
  let i = 0;
  while (bytes >= 1024 && i < units.length - 1) {
    bytes /= 1024;
    i++;
  }
  return `${bytes.toFixed(i === 0 ? 0 : digits)} ${units[i]}`;
}

/** Coletor periódico ligado a uma sessão. Emite 'stats' a cada intervalo. */
export class StatsCollector extends EventEmitter {
  /**
   * @param {import('./StreamBase.js').StreamBase} session
   * @param {() => object|null} getTargetQuality
   */
  constructor(session, getTargetQuality) {
    super();
    this.session = session;
    this.getTargetQuality = getTargetQuality;
    this.video = emptySummary('video');
    this.audio = emptySummary('audio');
    this.history = { video: [], audio: [] };
    this._timer = null;
  }

  start(intervalMs = 1000) {
    this.stop();
    this._timer = setInterval(() => this.collect(), intervalMs);
  }

  stop() {
    clearInterval(this._timer);
    this._timer = null;
  }

  async collect() {
    const pc = this.session.pc;
    if (!pc || pc.connectionState === 'closed') return;
    let report;
    try {
      report = await pc.getStats();
    } catch {
      return;
    }
    const isInbound = this.session.isInbound;
    const target = this.getTargetQuality?.() ?? null;
    for (const kind of ['video', 'audio']) {
      const summary = summarizeReport(report, this[kind], { kind, isInbound });
      analyzeIssues(summary, target, !isInbound);
      this[kind] = summary;
      const hist = this.history[kind];
      hist.push({ timestamp: Date.now(), value: summary });
      if (hist.length > HISTORY_LENGTH) hist.shift();
    }
    this.emit('stats', this);
  }

  get worstSeverity() {
    return Math.max(this.video.issueStatus & Severity.MAJOR ? 2 : this.video.issueStatus ? 1 : 0,
      this.audio.issueStatus & Severity.MAJOR ? 2 : this.audio.issueStatus ? 1 : 0);
  }
}
