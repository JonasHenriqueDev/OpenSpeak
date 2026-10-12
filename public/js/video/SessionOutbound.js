// Sessão transmissor -> um espectador (com::teamspeak::video::SessionOutbound).

import { StreamBase } from './StreamBase.js';
import { rankCodecs, applyCodecPreferences, scalabilityModeFor, mungeOpusStereo } from './codecs.js';

export class SessionOutbound extends StreamBase {
  /**
   * @param {object} o  ver StreamBase
   * @param {import('./SharedVideoSource.js').SharedVideoSource} o.source
   * @param {string[]} o.remoteCodecs   codecs que o espectador decodifica
   * @param {{bitrate:number, audioBitrate:number, quality:object}} o.encoding
   * @param {string} [o.preferredCodec]
   */
  constructor(o) {
    super({ ...o, isInbound: false });
    this.source = o.source;
    this.remoteCodecs = o.remoteCodecs ?? [];
    this.encoding = { ...o.encoding };
    this.preferredCodec = o.preferredCodec ?? 'auto';
    this.activeCodec = null;
    this.videoTransceiver = null;
    this.audioTransceiver = null;
    this._onTracksChanged = () => this.#syncTracks();
  }

  async start() {
    this.source.addSink(this);
    this.source.on('trackschanged', this._onTracksChanged);
    this.createPeerConnection();
    await this.setupPeerConnection();
    await this.startOfferAnswer();
  }

  /** Transceivers + codec + parâmetros numa PeerConnection (nova ou recriada). */
  async setupPeerConnection() {
    const pc = this.pc;
    const msid = new MediaStream();
    const scalabilityMode = await this.#configureVideo(pc, msid);
    this.audioTransceiver = pc.addTransceiver('audio', {
      direction: 'sendonly',
      streams: [msid],
      sendEncodings: [{ maxBitrate: this.encoding.audioBitrate, priority: 'high', networkPriority: 'high' }],
    });
    await this.#syncTracks();
    await this.applyEncoding(this.encoding);
    console.info(`${this.tag} codec=${this.activeCodec} scalability=${scalabilityMode ?? 'none'}`);
  }

  async #configureVideo(pc, msid) {
    const ranked = await rankCodecs(this.remoteCodecs, {
      quality: this.encoding.quality, bitrate: this.encoding.bitrate, preferred: this.preferredCodec,
    });
    this.activeCodec = ranked[0] ?? null;
    const scalabilityMode = this.activeCodec ? scalabilityModeFor(this.activeCodec) : null;
    const encoding = { maxBitrate: this.encoding.bitrate, maxFramerate: this.encoding.quality.fps, priority: 'high' };
    try {
      this.videoTransceiver = pc.addTransceiver('video', {
        direction: 'sendonly',
        streams: [msid],
        sendEncodings: [scalabilityMode ? { ...encoding, scalabilityMode } : encoding],
      });
    } catch {
      this.videoTransceiver = pc.addTransceiver('video', { direction: 'sendonly', streams: [msid], sendEncodings: [encoding] });
    }
    if (ranked.length) applyCodecPreferences(this.videoTransceiver, ranked);
    return scalabilityMode;
  }

  /** Aplica bitrate/FPS sem renegociar (RTCRtpSender.setParameters). */
  async applyEncoding(encoding) {
    this.encoding = { ...this.encoding, ...encoding };
    const sender = this.videoTransceiver?.sender;
    if (sender) {
      const params = sender.getParameters();
      if (!params.encodings?.length) params.encodings = [{}];
      params.encodings[0].maxBitrate = this.encoding.bitrate;
      params.encodings[0].maxFramerate = this.encoding.quality.fps;
      params.degradationPreference = this.source.degradationPreference;
      try {
        await sender.setParameters(params);
      } catch (err) {
        console.warn(`${this.tag} setParameters(video) falhou`, err);
      }
    }
    const audioSender = this.audioTransceiver?.sender;
    if (audioSender) {
      const params = audioSender.getParameters();
      if (!params.encodings?.length) params.encodings = [{}];
      params.encodings[0].maxBitrate = this.encoding.audioBitrate;
      try {
        await audioSender.setParameters(params);
      } catch (err) {
        console.warn(`${this.tag} setParameters(audio) falhou`, err);
      }
    }
  }

  /**
   * SessionOutbound::OnRemoteSupportedCodecsChanged — se um codec melhor
   * ficou disponível (ou o atual deixou de ser suportado), realinha.
   */
  async updateRemoteCodecs(codecs) {
    this.remoteCodecs = codecs;
    const ranked = await rankCodecs(codecs, {
      quality: this.encoding.quality, bitrate: this.encoding.bitrate, preferred: this.preferredCodec,
    });
    if (!ranked.length || ranked[0] === this.activeCodec) return;
    console.info(`${this.tag} realinhando codec ${this.activeCodec} -> ${ranked[0]}`);
    this.activeCodec = ranked[0];
    applyCodecPreferences(this.videoTransceiver, ranked);
    await this.startOfferAnswer();
  }

  transformLocalSdp(sdp) {
    return this.source.audioTrack ? mungeOpusStereo(sdp, this.encoding.audioBitrate) : sdp;
  }

  async #syncTracks() {
    try {
      await this.videoTransceiver?.sender.replaceTrack(this.source.outgoingVideoTrack);
      await this.audioTransceiver?.sender.replaceTrack(this.source.outgoingAudioTrack);
    } catch (err) {
      console.warn(`${this.tag} replaceTrack falhou`, err);
    }
  }

  close() {
    this.source.off('trackschanged', this._onTracksChanged);
    this.source.removeSink(this);
    super.close();
  }
}
