// Seleção dinâmica de codec ("dynamic codec selection" do TS 6).
//
// O espectador anuncia os codecs que sabe decodificar no `joinstreamrequest`;
// o transmissor escolhe, por sessão, o melhor codec comum. A ordem leva em conta
// se o codificador local é eficiente (aceleração por hardware) segundo a
// MediaCapabilities API, depois a eficiência de compressão AV1 > VP9 > H264 > VP8.

const COMPRESSION_ORDER = ['video/AV1', 'video/VP9', 'video/H264', 'video/VP8'];
const AUX_CODECS = new Set(['video/rtx', 'video/red', 'video/ulpfec', 'video/flexfec-03']);

const encodeInfoCache = new Map();

export function videoCodecMimeTypes(kind = 'receiver') {
  const caps = (kind === 'sender' ? RTCRtpSender : RTCRtpReceiver).getCapabilities?.('video');
  if (!caps) return [];
  const seen = new Set();
  for (const codec of caps.codecs) {
    if (!AUX_CODECS.has(codec.mimeType)) seen.add(codec.mimeType);
  }
  return [...seen];
}

/** Codecs que este cliente consegue decodificar (enviado no pedido de entrada). */
export function localDecodeCodecs() {
  return videoCodecMimeTypes('receiver');
}

async function encodingInfo(mimeType, quality, bitrate) {
  const key = `${mimeType}|${quality.width}x${quality.height}@${quality.fps}|${bitrate}`;
  if (encodeInfoCache.has(key)) return encodeInfoCache.get(key);
  let info = { supported: true, smooth: true, powerEfficient: false };
  if (navigator.mediaCapabilities?.encodingInfo) {
    try {
      const width = quality.width > 0 ? quality.width : screen.width * devicePixelRatio;
      const height = quality.height > 0 ? quality.height : screen.height * devicePixelRatio;
      info = await navigator.mediaCapabilities.encodingInfo({
        type: 'webrtc',
        video: {
          contentType: mimeType,
          width: Math.round(width),
          height: Math.round(height),
          bitrate,
          framerate: quality.fps,
          scalabilityMode: 'L1T3',
        },
      });
    } catch {
      // Navegadores antigos não aceitam scalabilityMode/webrtc: mantém o padrão.
    }
  }
  encodeInfoCache.set(key, info);
  return info;
}

/**
 * Ordena os codecs comuns entre local (envio) e remoto (decodificação).
 * @param {string[]} remoteCodecs
 * @param {{quality:object, bitrate:number, preferred?:string}} opts
 * @returns {Promise<string[]>} mimeTypes, melhor primeiro
 */
export async function rankCodecs(remoteCodecs, { quality, bitrate, preferred = 'auto' }) {
  const local = videoCodecMimeTypes('sender');
  const remote = new Set(remoteCodecs?.length ? remoteCodecs : local);
  const common = local.filter(m => remote.has(m));
  if (!common.length) return [];

  const scored = await Promise.all(common.map(async mime => {
    const info = await encodingInfo(mime, quality, bitrate);
    const order = COMPRESSION_ORDER.indexOf(mime);
    let score = (info.supported ? 0 : -100) + (info.powerEfficient ? 20 : 0) + (info.smooth ? 10 : 0);
    score += order === -1 ? 0 : (COMPRESSION_ORDER.length - order);
    if (preferred !== 'auto' && mime === preferred) score += 1000;
    return { mime, score };
  }));
  scored.sort((a, b) => b.score - a.score);
  return scored.map(s => s.mime);
}

/**
 * Aplica a preferência ao transceiver: codecs escolhidos primeiro, mantendo
 * RTX/RED/FEC para recuperação de perdas.
 */
export function applyCodecPreferences(transceiver, rankedMimeTypes) {
  if (!transceiver.setCodecPreferences) return false;
  const caps = RTCRtpReceiver.getCapabilities('video');
  if (!caps) return false;
  const ordered = [];
  for (const mime of rankedMimeTypes) ordered.push(...caps.codecs.filter(c => c.mimeType === mime));
  ordered.push(...caps.codecs.filter(c => AUX_CODECS.has(c.mimeType)));
  if (!ordered.length) return false;
  try {
    transceiver.setCodecPreferences(ordered);
    return true;
  } catch (err) {
    console.warn('[codecs] setCodecPreferences falhou', err);
    return false;
  }
}

/** Retorna o modo de escalabilidade temporal suportado pelo codec, se houver. */
export function scalabilityModeFor(mimeType) {
  const caps = RTCRtpSender.getCapabilities?.('video');
  if (!caps) return null;
  const codec = caps.codecs.find(c => c.mimeType === mimeType && Array.isArray(c.scalabilityModes));
  // Alguns navegadores não expõem scalabilityModes mas aceitam L1T3 no
  // addTransceiver; tentamos e SessionOutbound recua se for rejeitado.
  if (!codec) return caps.codecs.some(c => Array.isArray(c.scalabilityModes)) ? null : 'L1T3';
  return codec.scalabilityModes.includes('L1T3') ? 'L1T3' : codec.scalabilityModes.includes('L1T2') ? 'L1T2' : null;
}

/** Ajusta o fmtp do Opus para áudio estéreo de alta qualidade (áudio do sistema). */
export function mungeOpusStereo(sdp, maxAverageBitrate) {
  const match = sdp.match(/a=rtpmap:(\d+) opus\/48000\/2/i);
  if (!match) return sdp;
  const pt = match[1];
  const fmtpRe = new RegExp(`a=fmtp:${pt} ([^\\r\\n]*)`);
  const extra = { stereo: '1', 'sprop-stereo': '1', maxaveragebitrate: String(maxAverageBitrate), usedtx: '0' };
  if (fmtpRe.test(sdp)) {
    return sdp.replace(fmtpRe, (_, params) => {
      const map = new Map(params.split(';').filter(Boolean).map(p => p.split('=').map(s => s.trim())));
      for (const [k, v] of Object.entries(extra)) map.set(k, v);
      return `a=fmtp:${pt} ${[...map].map(([k, v]) => `${k}=${v}`).join(';')}`;
    });
  }
  return sdp.replace(match[0], `${match[0]}\r\na=fmtp:${pt} ${Object.entries(extra).map(([k, v]) => `${k}=${v}`).join(';')}`);
}
