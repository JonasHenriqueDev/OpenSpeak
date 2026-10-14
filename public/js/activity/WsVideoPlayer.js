// Player do espectador por WebSocket: RTP H.264 -> WebCodecs -> <canvas>.
// Prioriza latência: cada quadro é desenhado na chegada + `delayMs` (o atraso
// do buffer de áudio, para a imagem não ficar adiantada em relação ao som) e,
// se a fila do decodificador crescer, descarta até o próximo keyframe.

import { H264Depacketizer, MediaKind, avcCodecString, rtpClock, splitBatch } from './rtpH264.js';

const MAX_DECODE_QUEUE = 8;
const MAX_PRESENT_QUEUE = 30;
const KEYFRAME_REQUEST_INTERVAL_MS = 1000;

export class WsVideoPlayer {
  /**
   * @param {object} o
   * @param {HTMLCanvasElement} o.canvas
   * @param {() => void} o.requestKeyFrame  pede um IDR ao relay
   * @param {(packet: Uint8Array) => void} [o.onAudio]  pacotes RTP de áudio (Opus)
   */
  constructor({ canvas, requestKeyFrame, onAudio = () => {} }) {
    this.onAudio = onAudio;
    this.delayMs = 0;
    this.arrivals = new Map(); // timestamp do chunk -> chegada (performance.now)
    this.present = [];         // [{ frame, due }] em ordem
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.requestKeyFrameRaw = requestKeyFrame;
    this.lastKeyRequest = 0;
    this.decoder = null;
    this.codec = null;
    this.raf = 0;
    this.stats = { frames: 0, decoded: 0, losses: 0, keyFrames: 0, bytes: 0, width: 0, height: 0, fps: 0, kbps: 0, drops: 0 };
    this.#window = { at: performance.now(), frames: 0, bytes: 0 };
    this.clock = rtpClock(90000);
    this.depacketizer = new H264Depacketizer({
      onFrame: frame => this.#decode(frame),
      onLoss: () => { this.stats.losses++; this.#requestKeyFrame(); },
    });
  }

  #window;

  static get supported() {
    return typeof VideoDecoder === 'function';
  }

  /** Mensagem binária vinda do relay. */
  push(buffer) {
    this.stats.bytes += buffer.byteLength;
    this.#window.bytes += buffer.byteLength;
    for (const { kind, packet } of splitBatch(buffer)) {
      if (kind === MediaKind.VIDEO) this.depacketizer.push(packet);
      else if (kind === MediaKind.AUDIO) this.onAudio(packet);
    }
  }

  #requestKeyFrame() {
    const now = performance.now();
    if (now - this.lastKeyRequest < KEYFRAME_REQUEST_INTERVAL_MS) return;
    this.lastKeyRequest = now;
    this.requestKeyFrameRaw();
  }

  #ensureDecoder(frame) {
    if (!frame.key || !frame.sps) return !!this.decoder;
    const codec = avcCodecString(frame.sps);
    if (this.decoder?.state === 'configured' && codec === this.codec) return true;
    this.decoder?.state !== 'closed' && this.decoder?.close();
    this.decoder = new VideoDecoder({
      output: f => this.#present(f),
      error: err => {
        console.warn('[activity] decodificador:', err);
        this.decoder = null;
        this.depacketizer.resync();
        this.#requestKeyFrame();
      },
    });
    // Sem `description`: o fluxo é Annex B com SPS/PPS no próprio keyframe.
    this.decoder.configure({ codec, optimizeForLatency: true });
    this.codec = codec;
    return true;
  }

  #decode(frame) {
    if (!this.#ensureDecoder(frame)) {
      this.depacketizer.resync();
      this.#requestKeyFrame();
      return;
    }
    if (this.decoder.decodeQueueSize > MAX_DECODE_QUEUE && !frame.key) {
      this.stats.drops++;
      this.depacketizer.resync();
      this.#requestKeyFrame();
      return;
    }
    if (frame.key) this.stats.keyFrames++;
    const timestamp = this.clock(frame.timestamp);
    this.arrivals.set(timestamp, performance.now());
    if (this.arrivals.size > 64) this.arrivals.delete(this.arrivals.keys().next().value);
    try {
      this.decoder.decode(new EncodedVideoChunk({ type: frame.key ? 'key' : 'delta', timestamp, data: frame.data }));
    } catch (err) {
      console.warn('[activity] decode:', err);
      this.depacketizer.resync();
      this.#requestKeyFrame();
    }
  }

  #present(frame) {
    this.stats.decoded++;
    const arrival = this.arrivals.get(frame.timestamp) ?? performance.now();
    this.arrivals.delete(frame.timestamp);
    this.present.push({ frame, due: arrival + this.delayMs });
    while (this.present.length > MAX_PRESENT_QUEUE) this.present.shift().frame.close();
    if (!this.raf) this.raf = requestAnimationFrame(() => this.#draw());
  }

  #draw() {
    this.raf = 0;
    const now = performance.now();
    // O quadro mais novo já vencido; os anteriores a ele são descartados.
    let index = -1;
    for (let i = 0; i < this.present.length && this.present[i].due <= now; i++) index = i;
    if (index >= 0) {
      const [{ frame }] = this.present.splice(index, 1);
      for (const old of this.present.splice(0, index)) old.frame.close();
      this.#paint(frame);
    }
    if (this.present.length) this.raf = requestAnimationFrame(() => this.#draw());
  }

  #paint(frame) {
    const { displayWidth: w, displayHeight: h } = frame;
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
    }
    this.ctx.drawImage(frame, 0, 0, w, h);
    frame.close();
    this.stats.frames++;
    this.stats.width = w;
    this.stats.height = h;
    this.#window.frames++;
    const now = performance.now();
    const elapsed = now - this.#window.at;
    if (elapsed >= 1000) {
      this.stats.fps = Math.round(this.#window.frames * 1000 / elapsed);
      this.stats.kbps = Math.round(this.#window.bytes * 8 / elapsed);
      this.#window = { at: now, frames: 0, bytes: 0 };
    }
  }

  dispose() {
    cancelAnimationFrame(this.raf);
    for (const { frame } of this.present) frame.close();
    this.present = [];
    if (this.decoder && this.decoder.state !== 'closed') this.decoder.close();
    this.decoder = null;
  }
}
