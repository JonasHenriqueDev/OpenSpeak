// Mídia do relay por WebSocket (espectador sem WebRTC, ver native/server/SfuWsViewer.h).
// Cada mensagem binária traz registros [tipo u8][tamanho u16 BE][pacote RTP];
// o vídeo é H.264 (RFC 6184) e vira access units Annex B para o WebCodecs.

export const MediaKind = Object.freeze({ VIDEO: 0, AUDIO: 1 });

/** Separa uma mensagem binária do relay em pacotes RTP. */
export function* splitBatch(buffer) {
  const bytes = new Uint8Array(buffer);
  for (let i = 0; i + 3 <= bytes.length;) {
    const kind = bytes[i];
    const size = (bytes[i + 1] << 8) | bytes[i + 2];
    if (i + 3 + size > bytes.length) return;
    yield { kind, packet: bytes.subarray(i + 3, i + 3 + size) };
    i += 3 + size;
  }
}

/** Cabeçalho RTP; null se o pacote for inválido. */
export function parseRtp(p) {
  if (p.length < 12 || p[0] >> 6 !== 2) return null;
  let at = 12 + 4 * (p[0] & 0x0f);
  if (p[0] & 0x10) {
    if (p.length < at + 4) return null;
    at += 4 + 4 * ((p[at + 2] << 8) | p[at + 3]);
  }
  let end = p.length;
  if (p[0] & 0x20) end -= p[end - 1]; // padding
  if (end < at) return null;
  return {
    marker: (p[1] & 0x80) !== 0,
    seq: (p[2] << 8) | p[3],
    timestamp: ((p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7]) >>> 0,
    payload: p.subarray(at, end), // vazio: pacote só de padding (sondagem de banda)
  };
}

const START_CODE = new Uint8Array([0, 0, 0, 1]);
const NAL = Object.freeze({ IDR: 5, SPS: 7, PPS: 8, STAP_A: 24, FU_A: 28 });

/**
 * Remonta frames H.264 a partir do RTP. Uma perda (salto de seq) descarta o
 * frame atual e tudo até o próximo keyframe; `onLoss` avisa para pedir um IDR.
 */
export class H264Depacketizer {
  constructor({ onFrame, onLoss = () => {} }) {
    this.onFrame = onFrame;
    this.onLoss = onLoss;
    this.lastSeq = null;
    this.waitKey = true;
    this.#reset(null);
  }

  push(packet) {
    const rtp = parseRtp(packet);
    if (!rtp) return;
    if (this.lastSeq !== null) {
      const delta = (rtp.seq - this.lastSeq) & 0xffff;
      if (delta === 0 || delta > 0x8000) return; // duplicado ou atrasado
      if (delta !== 1) this.#lose();
    }
    this.lastSeq = rtp.seq;
    if (!rtp.payload.length) return; // padding também consome seq
    if (this.timestamp !== null && rtp.timestamp !== this.timestamp) this.#emit();
    this.timestamp = rtp.timestamp;
    this.#payload(rtp.payload);
    if (rtp.marker) this.#emit();
  }

  #payload(p) {
    const type = p[0] & 0x1f;
    if (type >= 1 && type <= 23) {
      this.#nal(p);
    } else if (type === NAL.STAP_A) {
      for (let i = 1; i + 2 <= p.length;) {
        const size = (p[i] << 8) | p[i + 1];
        if (!size || i + 2 + size > p.length) break;
        this.#nal(p.subarray(i + 2, i + 2 + size));
        i += 2 + size;
      }
    } else if (type === NAL.FU_A && p.length > 2) {
      const start = p[1] & 0x80, end = p[1] & 0x40;
      if (start) {
        this.fu = [new Uint8Array([(p[0] & 0xe0) | (p[1] & 0x1f)]), p.subarray(2)];
      } else if (this.fu) {
        this.fu.push(p.subarray(2));
      } else {
        this.broken = true; // fragmento sem o início
        return;
      }
      if (end && this.fu) {
        this.#nal(concat(this.fu));
        this.fu = null;
      }
    }
  }

  #nal(nal) {
    const type = nal[0] & 0x1f;
    if (type === NAL.IDR) this.key = true;
    if (type === NAL.SPS) this.sps = nal.slice();
    this.parts.push(START_CODE, nal.slice());
  }

  #emit() {
    const { parts, key, sps, broken, timestamp } = this;
    this.#reset(timestamp);
    if (!parts.length || broken || this.fu) return;
    if (this.waitKey && !key) return;
    this.waitKey = false;
    this.onFrame({ data: concat(parts), key, sps, timestamp });
  }

  #lose() {
    this.#reset(this.timestamp);
    this.broken = true;
    this.waitKey = true;
    this.onLoss();
  }

  /** Força esperar o próximo keyframe (ex.: erro no decodificador). */
  resync() {
    this.waitKey = true;
  }

  #reset(timestamp) {
    this.parts = [];
    this.key = false;
    this.sps = null;
    this.broken = false;
    this.fu = null;
    this.timestamp = timestamp;
  }
}

/** Timestamp RTP de 32 bits -> microssegundos monotônicos (trata a volta do contador). */
export function rtpClock(clockRate) {
  let last = null, high = 0;
  return ts => {
    if (last !== null && ts < last && last - ts > 0x80000000) high += 0x100000000;
    last = ts;
    return Math.round((high + ts) * (1e6 / clockRate));
  };
}

/** Codec WebCodecs a partir do SPS (perfil, restrições e nível). */
export function avcCodecString(sps) {
  const hex = b => b.toString(16).padStart(2, '0');
  return `avc1.${hex(sps[1])}${hex(sps[2])}${hex(sps[3])}`;
}

function concat(chunks) {
  let size = 0;
  for (const c of chunks) size += c.length;
  const out = new Uint8Array(size);
  let at = 0;
  for (const c of chunks) { out.set(c, at); at += c.length; }
  return out;
}
