// Sessão espectador <- transmissor (com::teamspeak::video::SessionInbound).

import { StreamBase } from './StreamBase.js';
import { mungeOpusStereo } from './codecs.js';

const OFFER_WAIT_MS = 10_000;

export class SessionInbound extends StreamBase {
  constructor(o) {
    super({ ...o, isInbound: true });
    this.mediaStream = new MediaStream();
    this._offerTimer = null;
  }

  async start() {
    this.createPeerConnection();
    await this.setupPeerConnection();
    // O transmissor oferta após `notifystreamclientjoined`; se a oferta não
    // chegar, pedimos uma nova (kReconnect).
    this._offerTimer = setTimeout(() => {
      if (!this.pc?.remoteDescription) this.reconnect();
    }, OFFER_WAIT_MS);
  }

  async setupPeerConnection() {
    const pc = this.pc;
    pc.addEventListener('track', ev => {
      const track = ev.track;
      // Baixa latência: o TS usa um buffer próprio (VideoAudioSyncBuffer);
      // aqui pedimos ao jitter buffer o menor alvo razoável.
      try {
        if ('jitterBufferTarget' in ev.receiver) ev.receiver.jitterBufferTarget = 80;
      } catch {
        // não suportado
      }
      for (const old of this.mediaStream.getTracks().filter(t => t.kind === track.kind)) this.mediaStream.removeTrack(old);
      this.mediaStream.addTrack(track);
      this.emit('track', track);
    });
  }

  async onSignalingMessage(payload) {
    if (payload.type === 'offer') clearTimeout(this._offerTimer);
    return super.onSignalingMessage(payload);
  }

  transformLocalSdp(sdp, kind) {
    return kind === 'answer' ? mungeOpusStereo(sdp, 510_000) : sdp;
  }

  close() {
    clearTimeout(this._offerTimer);
    this.mediaStream.getTracks().forEach(t => t.stop());
    super.close();
  }
}
