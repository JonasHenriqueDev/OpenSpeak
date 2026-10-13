// Painel de estatísticas do stream (gráficos + detalhes + problemas),
// com as mesmas métricas e chaves de severidade do TS 6.

import { h, replaceChildren } from './dom.js';
import { icon } from './icons.js';
import { t } from './i18n.js';
import { Severity, RELAY_CLID } from '../core/protocol.js';
import { formatBitrate, formatBytes } from '../video/StatsCollector.js';

const ms = v => `${Math.round(1000 * (v || 0))}ms`;
const DTLS_VERSIONS = { FEFF: 'DTLS 1.0', FEFD: 'DTLS 1.2', FEFC: 'DTLS 1.3' };

function graphLayers(isInbound, kind) {
  const layers = [
    { key: 'network:packetloss', title: t('video.stream.stats.packet_loss'), value: s => s.fractionLost, label: v => `${+(100 * v).toFixed(2)}%`, min: 0.1 },
    { key: 'network:jitter', title: t('video.stream.stats.jitter.title'), value: s => s.jitter, label: ms, min: 0.1 },
    isInbound ? null : { key: 'network:rtt', title: t('video.stream.stats.ping'), value: s => s.lastRoundTripTime / 2, label: ms, min: 0.1 },
    { key: 'network:retransmit', title: t('video.stream.stats.retransmit.title'), value: s => s.retransmit.bps, label: v => formatBitrate(v), min: 100_000 },
    { key: null, title: t('video.stream.stats.bitrate'), value: s => (isInbound ? s.received.bps : s.sent.bps), label: v => formatBitrate(v), min: 100_000 },
  ];
  if (kind === 'video') {
    layers.push(isInbound
      ? { key: null, title: t('video.stream.stats.decode_time.title'), value: s => s.video.decode?.avgDecodeTime ?? 0, label: ms, min: 0.02 }
      : { key: 'video:encodetime', title: t('video.stream.stats.encode_time.title'), value: s => s.video.encode?.avgEncodeTime ?? 0, label: ms, min: 0.02 });
    layers.push({ key: 'video:fps', title: t('video.stream.stats.fps.title'), value: s => s.video.fps, label: v => `${Math.round(v)}`, min: 30 });
  }
  return layers.filter(Boolean);
}

function drawGraph(canvas, samples, layer, severity) {
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || 220;
  const hgt = canvas.clientHeight || 56;
  if (canvas.width !== w * dpr) {
    canvas.width = w * dpr;
    canvas.height = hgt * dpr;
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, hgt);
  const values = samples.map(s => Number(layer.value(s.value)) || 0);
  const max = Math.max(layer.min, ...values) * 1.15;
  const css = getComputedStyle(document.documentElement);
  const color = severity === Severity.MAJOR ? css.getPropertyValue('--danger')
    : severity === Severity.MINOR ? css.getPropertyValue('--warn') : css.getPropertyValue('--accent');
  ctx.strokeStyle = css.getPropertyValue('--grid-line');
  ctx.lineWidth = 1;
  for (let i = 1; i < 4; i++) {
    const y = Math.round((hgt * i) / 4) + 0.5;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }
  if (values.length < 2) return;
  const step = w / (60 - 1);
  const x0 = w - (values.length - 1) * step;
  ctx.beginPath();
  values.forEach((v, i) => {
    const x = x0 + i * step;
    const y = hgt - (v / max) * (hgt - 4) - 2;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.strokeStyle = color.trim();
  ctx.lineWidth = 1.6;
  ctx.stroke();
  ctx.lineTo(x0 + (values.length - 1) * step, hgt);
  ctx.lineTo(x0, hgt);
  ctx.closePath();
  ctx.globalAlpha = 0.15;
  ctx.fillStyle = color.trim();
  ctx.fill();
  ctx.globalAlpha = 1;
}

export class StatsPanel {
  constructor({ handler, manager, streamIdentifier }) {
    this.handler = handler;
    this.manager = manager;
    this.streamIdentifier = streamIdentifier;
    this.title = t('video.stream.stats.title');
    this.tab = 'video';
    this.viewerClid = null;
    this.el = h('div.panel-body.stats');
    this.graphs = [];
  }

  /** StatsCollector atualmente exibido. */
  #collector() {
    const own = this.manager.ownStreams.get(this.streamIdentifier);
    if (own) {
      if (!own.sessions.has(this.viewerClid)) this.viewerClid = own.sessions.keys().next().value ?? null;
      return { own, collector: own.sessions.get(this.viewerClid)?.stats ?? null, target: own.quality };
    }
    const view = this.manager.inbound.get(this.streamIdentifier);
    return { own: null, collector: view?.stats ?? null, target: this.handler.streams.get(this.streamIdentifier)?.quality };
  }

  render() {
    const { own, collector, target } = this.#collector();
    const isInbound = !own;
    const header = [];
    if (own) {
      header.push(h('label.field.inline', h('span.field-label', t('video.stream.stats.viewer')),
        h('select.input', { on: { change: ev => { this.viewerClid = Number(ev.target.value); this.render(); } } },
          [...own.sessions.keys()].map(clid => h('option', { value: String(clid), selected: clid === this.viewerClid },
            clid === RELAY_CLID ? t('video.mode.server') : this.handler.clientName(clid))))));
    }
    if (!collector) {
      replaceChildren(this.el, header, h('p.muted', own ? t('video.stream.stats.no_viewers') : t('video.stream.ended')));
      this.graphs = [];
      return;
    }
    const tabs = h('div.segmented.small', ['video', 'audio'].map(k => h('button', {
      class: { active: this.tab === k },
      on: { click: () => { this.tab = k; this.render(); } },
    }, t(`video.stream.stats.${k}.title`))));

    const summary = collector[this.tab];
    this.issuesEl = h('div.issues');
    this.graphGrid = h('div.graph-grid');
    this.detailsEl = h('div.details');
    this.graphs = graphLayers(isInbound, this.tab).map(layer => {
      const canvas = h('canvas.graph');
      const valueEl = h('span.graph-value');
      const card = h('div.graph-card', h('div.graph-head', h('span', layer.title), valueEl), canvas);
      this.graphGrid.append(card);
      return { layer, canvas, valueEl, card };
    });
    replaceChildren(this.el, header, tabs, this.issuesEl, this.graphGrid, this.detailsEl);
    this.#update(collector, summary, target, isInbound);
  }

  /** Chamado a cada coleta (1 s). */
  refresh() {
    const { collector, target, own } = this.#collector();
    if (!collector || !this.graphs.length) {
      this.render();
      return;
    }
    this.#update(collector, collector[this.tab], target, !own);
  }

  #update(collector, s, target, isInbound) {
    const history = collector.history[this.tab];
    for (const g of this.graphs) {
      const sev = g.layer.key ? s.issueSeverity[g.layer.key] : 0;
      drawGraph(g.canvas, history, g.layer, sev);
      g.valueEl.textContent = g.layer.label(g.layer.value(s));
      g.card.classList.toggle('minor', sev === Severity.MINOR);
      g.card.classList.toggle('major', sev === Severity.MAJOR);
    }
    replaceChildren(this.issuesEl, s.issues.length
      ? s.issues.map(i => h('div.issue', { class: i.severity === Severity.MAJOR ? 'major' : 'minor' },
        icon(i.severity === Severity.MAJOR ? 'warning' : 'info'),
        t(`video.stream.issue.${i.category}.${i.name}`, { value: i.value, expected: i.expected })))
      : h('div.issue.ok', icon('check'), t('video.stream.issues.none')));

    const rows = [];
    const row = (key, value, sevKey) => rows.push(h('div.detail-row', {
      class: sevKey && s.issueSeverity[sevKey] ? (s.issueSeverity[sevKey] === Severity.MAJOR ? 'major' : 'minor') : '',
    }, h('span', t(key)), h('span.value', value)));
    row('video.stream.stats.codec', s.codec.replace(/^(video|audio)\//, '') || '—');
    if (this.tab === 'video') {
      if (isInbound) {
        row('video.stream.stats.video.decoder', s.video.decode?.decoder || '—');
      } else {
        row('video.stream.stats.video.encoder', `${s.video.encode?.encoder || '—'}${s.video.encode?.powerEfficient ? ' (HW)' : ''}`);
        row('video.stream.stats.video.scalability_mode', s.video.encode?.scalabilityMode || '—');
        row('video.stream.stats.video.limit.reason', s.video.encode?.limitReason ?? '—', 'video:qualitylimit');
      }
      if (target) {
        const tw = target.width > 0 ? `${target.width}x${target.height}` : t('video.preset.source');
        row('video.stream.stats.video.quality.target', `${tw} ${target.fps}fps`);
      }
      row('video.stream.stats.video.quality.current', `${s.video.width}x${s.video.height} ${s.video.fps}fps`, 'video:fps');
    }
    if (isInbound) {
      row('video.stream.stats.received.packets', s.received.packets.toLocaleString('pt-BR'));
      row('video.stream.stats.received.bytes', formatBytes(s.received.bytes));
      row('video.stream.stats.received.packets_lost', s.packetsLost.toLocaleString('pt-BR'), 'network:packetloss');
    } else {
      row('video.stream.stats.sent.packets', s.sent.packets.toLocaleString('pt-BR'));
      row('video.stream.stats.sent.bytes', formatBytes(s.sent.bytes));
      row('video.stream.stats.sent.packets_lost', s.packetsLost.toLocaleString('pt-BR'), 'network:packetloss');
      row('video.stream.stats.bitrate.target', formatBitrate(s.targetBitrate));
      row('video.stream.stats.sent.current_delay', ms(s.sent.currentDelay), 'network:packetdelay');
      row('video.stream.stats.sent.total_delay', `${s.sent.totalDelay.toFixed(2)}s`);
    }
    row('video.stream.stats.ping.avg', ms(s.avgRoundTripTime / 2), 'network:rtt');
    row('video.stream.stats.retransmit.packets', s.retransmit.packets.toLocaleString('pt-BR'), 'network:retransmit');
    row('video.stream.stats.retransmit.bytes', formatBytes(s.retransmit.bytes));
    row('video.stream.stats.connection_type', s.connectionType ? t(`video.stream.stats.connection.${s.connectionType}`) : '—');
    row('video.stream.stats.srtp_cipher', s.srtpCipher || '—');
    row('video.stream.stats.tls_version', DTLS_VERSIONS[s.tlsVersion] ?? (s.tlsVersion || '—'));
    replaceChildren(this.detailsEl, rows);
  }
}
