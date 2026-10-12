// Base de uma sessão P2P (equivalente a com::teamspeak::video::StreamBase).
//
// Mensagens de sinalização trafegam pelo servidor em `streamsignaling` com um
// payload espelhando Stream_Signaling_Commands_Base do TS:
//   { type: 'offer',  sdp, isReconnect }   kOffer
//   { type: 'answer', sdp }                kAnswer
//   { type: 'ice',    candidate }          kICECandidate (null = fim dos candidatos)
//   { type: 'reconnect' }                  kReconnect (espectador pede ICE restart)
//
// O lado de saída (transmissor) é sempre quem oferta, então não há "glare".

import { EventEmitter } from '../core/EventEmitter.js';

export const SessionState = Object.freeze({
  NEW: 'new',
  CONNECTING: 'connecting',
  CONNECTED: 'connected',
  RECONNECTING: 'reconnecting',
  FAILED: 'failed',
  CLOSED: 'closed',
});

const DISCONNECTED_GRACE_MS = 4_000;
const CONNECT_TIMEOUT_MS = 20_000;
const MAX_RECONNECT_ATTEMPTS = 5;

export class StreamBase extends EventEmitter {
  /**
   * @param {object} o
   * @param {string} o.streamIdentifier
   * @param {number} o.peerClid       cliente do outro lado
   * @param {object[]} o.iceServers
   * @param {(payload:object) => Promise<void>} o.sendSignaling
   * @param {boolean} o.isInbound
   */
  constructor({ streamIdentifier, peerClid, iceServers, sendSignaling, isInbound }) {
    super();
    this.streamIdentifier = streamIdentifier;
    this.peerClid = peerClid;
    this.iceServers = iceServers;
    this.isInbound = isInbound;
    this.state = SessionState.NEW;
    this.pc = null;
    this.reconnectAttempts = 0;
    this.lastError = null;
    this._sendSignaling = sendSignaling;
    this._pendingCandidates = [];
    this._negotiating = false;
    this._needsNegotiation = false;
    this._disconnectTimer = null;
    this._connectTimer = null;
  }

  get tag() {
    return `[${this.isInbound ? 'in' : 'out'} ${this.streamIdentifier}/${this.peerClid}]`;
  }

  createPeerConnection() {
    const pc = new RTCPeerConnection({
      iceServers: this.iceServers,
      bundlePolicy: 'max-bundle',
      rtcpMuxPolicy: 'require',
    });
    pc.addEventListener('icecandidate', ev => {
      if (pc !== this.pc) return;
      this.sendToPeer({ type: 'ice', candidate: ev.candidate ? ev.candidate.toJSON() : null });
    });
    pc.addEventListener('connectionstatechange', () => {
      if (pc === this.pc) this.#onConnectionChange();
    });
    this.pc = pc;
    if (this.state !== SessionState.RECONNECTING) this.#setState(SessionState.CONNECTING);
    this.#armConnectTimer();
    return pc;
  }

  /** Subclasses adicionam tracks/handlers numa PeerConnection nova. */
  async setupPeerConnection() {}

  /**
   * Reconexão = PeerConnection nova nos dois lados (compatível com o cliente
   * nativo, cujo libdatachannel não faz ICE restart).
   */
  async rebuildPeerConnection() {
    const old = this.pc;
    this.pc = null;
    try {
      old?.close();
    } catch {
      // ignorado
    }
    this._pendingCandidates = [];
    this._negotiating = false;
    this._needsNegotiation = false;
    this.createPeerConnection();
    await this.setupPeerConnection();
  }

  sendToPeer(payload) {
    if (this.state === SessionState.CLOSED) return;
    this._sendSignaling(payload).catch(err => {
      console.warn(`${this.tag} SendToPeer falhou`, err);
    });
  }

  /** Oferta (somente lado de saída). Serializa renegociações concorrentes. */
  async startOfferAnswer({ iceRestart = false, isReconnect = false } = {}) {
    if (!this.pc || this.state === SessionState.CLOSED) return;
    if (this._negotiating && isReconnect && this.pc.signalingState === 'have-local-offer') {
      // A resposta anterior se perdeu: descarta a oferta pendente e recomeça.
      try {
        await this.pc.setLocalDescription({ type: 'rollback' });
      } catch {
        // ignorado
      }
      this._negotiating = false;
    }
    if (this._negotiating) {
      this._needsNegotiation = true;
      this._needsIceRestart ||= iceRestart;
      return;
    }
    this._negotiating = true;
    try {
      const offer = await this.pc.createOffer({ iceRestart });
      offer.sdp = this.transformLocalSdp(offer.sdp, 'offer');
      await this.pc.setLocalDescription(offer);
      this.sendToPeer({ type: 'offer', sdp: this.pc.localDescription.sdp, isReconnect });
    } catch (err) {
      this._negotiating = false;
      console.error(`${this.tag} StartOfferAnswer: falha ao criar oferta`, err);
      this.fail(err);
    }
  }

  /** Roteado pelo ScreenshareManager a partir de notifystreamsignaling. */
  async onSignalingMessage(payload) {
    if (!this.pc || this.state === SessionState.CLOSED) return;
    try {
      switch (payload.type) {
        case 'offer':
          await this.#onOffer(payload);
          break;
        case 'answer':
          await this.#onAnswer(payload);
          break;
        case 'ice':
          await this.#onIceCandidate(payload.candidate);
          break;
        case 'reconnect':
          if (!this.isInbound) {
            console.info(`${this.tag} kReconnect recebido, recriando a conexão`);
            this.#setState(SessionState.RECONNECTING);
            await this.rebuildPeerConnection();
            await this.startOfferAnswer({ isReconnect: true });
          }
          break;
        default:
          console.warn(`${this.tag} sinalização desconhecida`, payload.type);
      }
    } catch (err) {
      console.error(`${this.tag} OnStreamSignalingMessage(${payload.type}) falhou`, err);
    }
  }

  /** Gancho para ajustes de SDP (ex.: Opus estéreo). */
  transformLocalSdp(sdp) {
    return sdp;
  }

  reconnect() {
    if (this.state === SessionState.CLOSED || this.state === SessionState.FAILED) return;
    this.reconnectAttempts++;
    if (this.reconnectAttempts > MAX_RECONNECT_ATTEMPTS) {
      this.fail(new Error('número máximo de reconexões atingido'));
      return;
    }
    console.info(`${this.tag} Reconnect tentativa ${this.reconnectAttempts}`);
    this.#setState(SessionState.RECONNECTING);
    this.#armConnectTimer();
    if (this.isInbound) this.sendToPeer({ type: 'reconnect' });
    else this.rebuildPeerConnection().then(() => this.startOfferAnswer({ isReconnect: true }));
  }

  fail(error) {
    if (this.state === SessionState.CLOSED || this.state === SessionState.FAILED) return;
    this.lastError = error;
    this.#setState(SessionState.FAILED);
    this.emit('failed', error);
  }

  close() {
    if (this.state === SessionState.CLOSED) return;
    clearTimeout(this._disconnectTimer);
    clearTimeout(this._connectTimer);
    this.#setState(SessionState.CLOSED);
    try {
      this.pc?.close();
    } catch {
      // ignorado
    }
    this.emit('closed');
    this.removeAllListeners();
  }

  // ----------------------------------------------------------- internos

  async #onOffer({ sdp, isReconnect }) {
    if (!this.isInbound) return;
    if (isReconnect && this.pc.remoteDescription) {
      console.info(`${this.tag} kOffer de reconexão: conexão nova`);
      await this.rebuildPeerConnection();
    }
    await this.pc.setRemoteDescription({ type: 'offer', sdp });
    await this.#flushCandidates();
    const answer = await this.pc.createAnswer();
    answer.sdp = this.transformLocalSdp(answer.sdp, 'answer');
    await this.pc.setLocalDescription(answer);
    this.sendToPeer({ type: 'answer', sdp: this.pc.localDescription.sdp });
  }

  async #onAnswer({ sdp }) {
    if (this.isInbound) return;
    if (this.pc.signalingState !== 'have-local-offer') {
      console.warn(`${this.tag} kAnswer fora de hora (${this.pc.signalingState})`);
      return;
    }
    try {
      await this.pc.setRemoteDescription({ type: 'answer', sdp });
      await this.#flushCandidates();
    } finally {
      this._negotiating = false;
    }
    this.emit('negotiated');
    if (this._needsNegotiation) {
      const iceRestart = !!this._needsIceRestart;
      this._needsNegotiation = false;
      this._needsIceRestart = false;
      await this.startOfferAnswer({ iceRestart });
    }
  }

  async #onIceCandidate(candidate) {
    if (!this.pc.remoteDescription) {
      this._pendingCandidates.push(candidate);
      return;
    }
    await this.#addCandidate(candidate);
  }

  async #flushCandidates() {
    const list = this._pendingCandidates;
    this._pendingCandidates = [];
    for (const c of list) await this.#addCandidate(c);
  }

  async #addCandidate(candidate) {
    try {
      await this.pc.addIceCandidate(candidate ?? null);
    } catch (err) {
      // candidatos de uma geração antiga após ICE restart são esperados
      if (candidate) console.debug(`${this.tag} candidato descartado`, err.message);
    }
  }

  #onConnectionChange() {
    const s = this.pc.connectionState;
    console.debug(`${this.tag} OnConnectionChange: ${s}`);
    clearTimeout(this._disconnectTimer);
    if (s === 'connected') {
      clearTimeout(this._connectTimer);
      this.reconnectAttempts = 0;
      this.#setState(SessionState.CONNECTED);
    } else if (s === 'disconnected') {
      this._disconnectTimer = setTimeout(() => {
        if (this.pc?.connectionState === 'disconnected') this.reconnect();
      }, DISCONNECTED_GRACE_MS);
    } else if (s === 'failed') {
      this.reconnect();
    }
  }

  #armConnectTimer() {
    clearTimeout(this._connectTimer);
    this._connectTimer = setTimeout(() => {
      if (this.state === SessionState.CONNECTING || this.state === SessionState.RECONNECTING) this.reconnect();
    }, CONNECT_TIMEOUT_MS);
  }

  #setState(state) {
    if (this.state === state) return;
    const prev = this.state;
    this.state = state;
    this.emit('statechange', state, prev);
  }
}
