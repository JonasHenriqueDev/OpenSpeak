// Áudio do espectador por WebSocket: RTP Opus -> AudioDecoder -> AudioContext.
//
// Cada pacote RTP de Opus é um quadro completo (RFC 7587). Os quadros entram
// numa agenda pelo timestamp RTP (48 kHz), com um buffer de LATENCY_S para
// absorver a irregularidade do WebSocket: um quadro atrasado reancora a agenda
// e um acúmulo acima de MAX_LATENCY_S (relógios do transmissor e da placa de
// som divergindo) também, descartando o excesso.

import { parseRtp, rtpClock } from './rtpH264.js';

const SAMPLE_RATE = 48000;
const LATENCY_S = 0.12;
const MAX_LATENCY_S = 0.4;

export class WsAudioPlayer {
  constructor() {
    this.context = null;
    this.decoder = null;
    this.gain = null;
    this.clock = rtpClock(SAMPLE_RATE);
    this.anchor = null; // { media: µs, time: s no AudioContext }
    this.lastPacketAt = 0;
    this.volume = 1;
    this.muted = false;
    this.error = null;
    this.stats = { frames: 0, resyncs: 0 };
  }

  static get supported() {
    return typeof AudioDecoder === 'function' && typeof AudioContext === 'function';
  }

  /** Chamar dentro do clique do usuário (política de autoplay). */
  unlock() {
    if (!WsAudioPlayer.supported) return;
    if (!this.context) {
      this.context = new AudioContext({ sampleRate: SAMPLE_RATE, latencyHint: 'interactive' });
      this.gain = this.context.createGain();
      this.gain.connect(this.context.destination);
      this.#applyVolume();
    }
    this.context.resume().catch(() => {});
  }

  /** Tocando agora (recebeu áudio há pouco)? */
  get active() {
    return !!this.decoder && performance.now() - this.lastPacketAt < 1000;
  }

  /** Atraso do som em relação à chegada, para o vídeo acompanhar. */
  get latencyMs() {
    return LATENCY_S * 1000;
  }

  push(packet) {
    if (!this.context || this.error) return;
    const rtp = parseRtp(packet);
    if (!rtp || !rtp.payload.length) return;
    this.lastPacketAt = performance.now();
    if (!this.decoder && !this.#configure()) return;
    try {
      this.decoder.decode(new EncodedAudioChunk({ type: 'key', timestamp: this.clock(rtp.timestamp), data: rtp.payload }));
    } catch (err) {
      console.warn('[activity] áudio:', err);
    }
  }

  #configure() {
    try {
      this.decoder = new AudioDecoder({
        output: data => this.#schedule(data),
        error: err => {
          console.warn('[activity] decodificador de áudio:', err);
          this.decoder = null;
          this.anchor = null;
        },
      });
      // Sem `description` (OpusHead): mapeamento padrão do WebRTC, estéreo 48 kHz.
      this.decoder.configure({ codec: 'opus', sampleRate: SAMPLE_RATE, numberOfChannels: 2 });
      return true;
    } catch (err) {
      this.error = err;
      this.decoder = null;
      console.warn('[activity] sem decodificador Opus:', err);
      return false;
    }
  }

  #schedule(data) {
    const ctx = this.context;
    try {
      const frames = data.numberOfFrames;
      const channels = Math.min(data.numberOfChannels, 2);
      const buffer = ctx.createBuffer(channels, frames, data.sampleRate);
      for (let ch = 0; ch < channels; ch++) {
        const plane = new Float32Array(frames);
        data.copyTo(plane, { planeIndex: ch, format: 'f32-planar' });
        buffer.copyToChannel(plane, ch);
      }
      let at = this.anchor ? this.anchor.time + (data.timestamp - this.anchor.media) / 1e6 : -1;
      const now = ctx.currentTime;
      if (at < now + 0.01 || at > now + MAX_LATENCY_S) {
        if (this.anchor) this.stats.resyncs++;
        this.anchor = { media: data.timestamp, time: now + LATENCY_S };
        at = this.anchor.time;
      }
      const source = ctx.createBufferSource();
      source.buffer = buffer;
      source.connect(this.gain);
      source.start(at);
      this.stats.frames++;
    } finally {
      data.close();
    }
  }

  setVolume(volume) {
    this.volume = volume;
    this.muted = volume === 0;
    this.#applyVolume();
  }

  toggleMute() {
    this.muted = !this.muted;
    if (!this.muted && this.volume === 0) this.volume = 1;
    this.#applyVolume();
  }

  #applyVolume() {
    if (this.gain) this.gain.gain.value = this.muted ? 0 : this.volume;
  }

  dispose() {
    if (this.decoder && this.decoder.state !== 'closed') this.decoder.close();
    this.decoder = null;
    this.context?.close().catch(() => {});
    this.context = null;
  }
}
