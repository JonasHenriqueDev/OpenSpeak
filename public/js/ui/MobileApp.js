// Versão mobile do site: lista quem está ao vivo, assiste a um stream por vez e
// transmite a câmera do aparelho com o microfone (tela fica só no computador).

import { h, replaceChildren } from './dom.js';
import { icon } from './icons.js';
import { t } from './i18n.js';
import { StreamAccess, StreamType, QUALITY_PRESETS } from '../core/protocol.js';
import { InboundStatus } from '../video/ScreenshareManager.js';
import { SharedVideoSource } from '../video/SharedVideoSource.js';
import { installFullscreenIdle } from './fullscreenIdle.js';

// 720p30 com bitrate menor que o do computador: rede móvel e aparelho esquentam.
const CAMERA_PRESET = QUALITY_PRESETS.find(p => p.id === '720');
const CAMERA_QUALITY = { width: CAMERA_PRESET.width, height: CAMERA_PRESET.height, fps: CAMERA_PRESET.fps, audioBitrate: CAMERA_PRESET.audioBitrate };
const CAMERA_BITRATE = 2_000_000;

const TYPE_ICON = { [StreamType.CAMERA]: 'camera', [StreamType.SCREEN]: 'screen', [StreamType.WINDOW]: 'window' };

export class MobileApp {
  /**
   * @param {object} o
   * @param {HTMLElement} o.root
   * @param {import('../core/ConnectionHandler.js').ConnectionHandler} o.handler
   * @param {import('../video/ScreenshareManager.js').ScreenshareManager} o.manager
   * @param {(err:Error) => void} o.onError
   * @param {() => void} o.onDisconnected
   */
  constructor({ root, handler, manager, onError, onDisconnected }) {
    this.root = root;
    this.handler = handler;
    this.manager = manager;
    this.onError = onError;
    this.onDisconnected = onDisconnected;
    this.watching = null;
    this.attached = null;
    this.needsTap = false; // o navegador só liberou o vídeo sem som

    this.video = h('video.mobile-video', { autoplay: true, playsInline: true });
    this.center = h('div.mobile-player-center');
    this.controls = h('div.mobile-controls');
    this.player = h('section.mobile-player', { hidden: true }, this.video, this.center, this.controls);
    this.list = h('div.mobile-list');
    this.status = h('span.muted');
    installFullscreenIdle(this.player);

    // Transmissão da câmera: null, ou { source, own, opening, busy, starting, canFlip }.
    this.cam = null;
    this.wakeLock = null;
    this.camVideo = h('video.mobile-video', { autoplay: true, muted: true, playsInline: true });
    this.camCenter = h('div.mobile-player-center');
    this.camControls = h('div.mobile-controls');
    this.camPanel = h('section.mobile-player.mobile-camera', { hidden: true }, this.camVideo, this.camCenter, this.camControls);
    // Sempre visível: quando não dá para transmitir, o toque explica o motivo.
    this.shareBtn = h('button.btn.primary.small.mobile-share', {
      title: t('video.mobile.camera.share'),
      on: { click: () => this.#onShareClick() },
    }, icon('camera'), t('video.mobile.camera.share_short'));
  }

  /** Motivo (chave de tradução) de não poder transmitir a câmera, ou null. */
  #cameraBlocker() {
    const hd = this.handler;
    if (!hd.hasStreamingSFU && !hd.hasStreamingP2P) return 'video.mobile.camera.unavailable.server';
    if (!window.isSecureContext) return 'video.mobile.camera.unavailable.https';
    if (!this.manager.isSupported || !navigator.mediaDevices?.getUserMedia) return 'video.mobile.camera.unavailable.browser';
    return null;
  }

  #onShareClick() {
    const blocker = this.#cameraBlocker();
    if (blocker) return void this.onError(new Error(t(blocker)));
    this.#openCamera();
  }

  mount() {
    const hd = this.handler;
    replaceChildren(this.root,
      h('div.mobile-app',
        h('header.mobile-header',
          icon('logo'), h('span.ellipsis', hd.serverName),
          h('span.spacer'),
          this.status,
          this.shareBtn,
          h('button.icon-btn', { title: t('app.disconnect'), 'aria-label': t('app.disconnect'), on: { click: () => hd.disconnect() } }, icon('power'))),
        h('div.mobile-body', this.camPanel, this.player, this.list)));

    hd.on('changed', () => this.render());
    hd.on('streamupdated', () => this.render());
    hd.on('reconnecting', () => this.render());
    hd.on('resumed', () => this.render());
    hd.on('disconnected', () => {
      this.#attach(null);
      this.#closeCamera();
      this.onDisconnected();
    });
    this.manager.on('changed', () => this.render());
    // A câmera pode cair sozinha (app em segundo plano, outra app pegou a câmera).
    this.manager.on('ownstreamstopped', own => {
      if (this.cam?.own === own) this.#closeCamera();
    });
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible' && this.cam?.own) this.#keepAwake();
    });
    document.addEventListener('fullscreenchange', () => this.render());
    window.addEventListener('beforeunload', () => {
      this.#closeCamera();
      hd.disconnect();
    });
    this.render();
  }

  render() {
    this.status.textContent = this.handler.reconnecting ? 'Reconectando…' : '';
    const view = this.watching ? this.manager.inbound.get(this.watching) : null;
    if (this.watching && !view) this.#stopWatching();
    this.shareBtn.hidden = !!this.cam;
    this.#renderCamera();
    this.#renderPlayer(view);
    this.#renderList();
  }

  // ---------------------------------------------------- transmitir câmera

  /** Abre a pré-visualização; precisa ser chamado dentro do toque (permissão). */
  async #openCamera() {
    if (this.cam) return;
    const cam = { source: null, own: null, opening: true, busy: false, starting: false, canFlip: false };
    this.cam = cam;
    this.render();
    let noMic;
    try {
      ({ source: cam.source, noMic } = await this.#openSource('user'));
    } catch (err) {
      if (this.cam !== cam) return; // fechou enquanto abria
      this.cam = null;
      this.render();
      this.onError(this.#captureError(err));
      return;
    }
    if (this.cam !== cam) return void cam.source.stop(); // fechou enquanto abria
    if (noMic) this.onError(new Error(t('video.mobile.camera.no_mic')));
    cam.opening = false;
    cam.source.on('ended', () => { if (this.cam === cam && !cam.own) this.#closeCamera(); });
    this.render();
    // Só com permissão dada a lista de câmeras é confiável; com uma só, não há o que virar.
    navigator.mediaDevices.enumerateDevices?.().then(devices => {
      cam.canFlip = devices.filter(d => d.kind === 'videoinput').length > 1;
      if (this.cam === cam) this.render();
    }).catch(() => {});
  }

  /** Câmera com microfone; sem permissão/aparelho de áudio, vai só o vídeo. */
  async #openSource(facingMode) {
    const o = { type: StreamType.CAMERA, facingMode, quality: CAMERA_QUALITY };
    try {
      return { source: await SharedVideoSource.open({ ...o, audio: true }), noMic: false };
    } catch (err) {
      const source = await SharedVideoSource.open({ ...o, audio: false }).catch(() => { throw err; });
      return { source, noMic: true };
    }
  }

  #captureError(err) {
    if (err?.name === 'NotAllowedError') return new Error(t('video.error.permission'));
    if (err?.name === 'NotFoundError' || err?.name === 'NotReadableError' || err?.name === 'OverconstrainedError') {
      return new Error(t('video.error.no_available_source'));
    }
    return err;
  }

  async #goLive() {
    const cam = this.cam;
    if (!cam?.source || cam.own || cam.busy) return;
    cam.busy = cam.starting = true;
    this.render();
    const hd = this.handler;
    const me = hd.ownClient;
    try {
      cam.own = await this.manager.startStream({
        config: {
          name: t('video.mobile.camera.name', { name: me ? hd.clientName(me.clid) : '' }).trim(),
          type: StreamType.CAMERA,
          access: StreamAccess.PUBLIC,
          bitrate: CAMERA_BITRATE,
          viewerLimit: 0,
          audio: !!cam.source.audioTrack,
          announce: true,
        },
        quality: { ...CAMERA_QUALITY },
        source: cam.source,
      });
      if (this.cam !== cam) this.manager.stopStream(cam.own.streamIdentifier).catch(() => {}); // fechou enquanto entrava
      else this.#keepAwake();
    } catch (err) {
      cam.source.stop(); // nem toda recusa do startStream fecha a fonte; repetir é inofensivo
      if (this.cam === cam) {
        this.cam = null;
        this.camVideo.srcObject = null;
        this.onError(err);
      }
    }
    cam.busy = cam.starting = false;
    this.render();
  }

  /** Para a transmissão (se houver) e fecha a câmera. */
  #closeCamera() {
    const cam = this.cam;
    if (!cam) return;
    this.cam = null;
    this.camVideo.srcObject = null;
    this.wakeLock?.release().catch(() => {});
    this.wakeLock = null;
    if (cam.own) this.manager.stopStream(cam.own.streamIdentifier).catch(() => {});
    else if (!cam.starting) cam.source?.stop(); // indo ao vivo: #goLive encerra
    this.render();
  }

  #toggleMic() {
    const track = this.cam?.source?.audioTrack;
    if (!track) return;
    track.enabled = !track.enabled;
    this.render();
  }

  async #flipCamera() {
    const cam = this.cam;
    if (!cam?.source || cam.busy) return;
    cam.busy = true;
    this.render();
    try {
      await cam.source.switchCamera(cam.source.facingMode === 'environment' ? 'user' : 'environment');
    } catch (err) {
      if (this.cam === cam) this.onError(this.#captureError(err)); // fechou enquanto trocava: sem erro
    }
    cam.busy = false;
    this.render();
  }

  /** Com a tela apagada o celular corta a câmera; mantém acesa enquanto ao vivo. */
  #keepAwake() {
    if (!navigator.wakeLock || this.wakeLock) return;
    navigator.wakeLock.request('screen').then(lock => {
      if (!this.cam?.own || this.wakeLock) return void lock.release().catch(() => {});
      this.wakeLock = lock;
      lock.addEventListener('release', () => { if (this.wakeLock === lock) this.wakeLock = null; });
    }).catch(() => {});
  }

  #renderCamera() {
    const cam = this.cam;
    this.camPanel.hidden = !cam;
    if (!cam) return;
    const source = cam.source;
    const track = source?.videoTrack ?? null;
    if (track && this.camVideo.srcObject?.getVideoTracks()[0] !== track) {
      this.camVideo.srcObject = new MediaStream([track]); // só o vídeo: sem eco do próprio microfone
      this.camVideo.play().catch(() => {});
    }
    this.camVideo.classList.toggle('mirrored', source?.facingMode !== 'environment');
    const live = !!cam.own;
    const info = live ? this.handler.streams.get(cam.own.streamIdentifier) : null;
    replaceChildren(this.camCenter,
      cam.opening ? h('div.status-pill', h('span.spinner'), t('video.mobile.camera.opening'))
        : live ? h('span.mobile-camera-badges',
          h('span.badge.live', t('video.stream.live')),
          h('span.viewer-count', icon('users'), String(info?.viewerCount ?? 0)))
          : null);
    const mic = source?.audioTrack;
    const micOn = !!mic && mic.enabled;
    replaceChildren(this.camControls,
      h('div.mobile-now',
        h('strong.ellipsis', t('video.mobile.camera.title')),
        h('span.muted.ellipsis', live ? info?.name ?? '' : t('video.mobile.camera.preview'))),
      source ? this.#control(micOn ? 'mic' : 'micOff',
        t(micOn ? 'video.mobile.camera.mic_on' : 'video.mobile.camera.mic_off'), () => this.#toggleMic(), mic ? '' : 'disabled') : null,
      source && cam.canFlip ? this.#control('swap', t('video.mobile.camera.flip'), () => this.#flipCamera(), cam.busy ? 'disabled' : '') : null,
      live
        ? this.#control('stop', t('video.mobile.camera.stop'), () => this.#closeCamera(), 'danger')
        : [
          source ? h('button.btn.primary.mobile-golive', { disabled: cam.busy, on: { click: () => this.#goLive() } }, t('video.mobile.camera.go_live')) : null,
          this.#control('close', t('video.mobile.camera.close'), () => this.#closeCamera()),
        ]);
  }

  /** Trocar de stream sai do anterior; assistir é sempre um por vez. */
  #watch(id) {
    if (id === this.watching) return;
    if (this.watching) this.#leave();
    this.watching = id;
    // Dentro do toque, para o navegador liberar o som quando o vídeo chegar.
    this.video.play().catch(() => {});
    this.manager.joinStream(id).catch(this.onError);
  }

  #leave() {
    const id = this.watching;
    this.#stopWatching();
    this.manager.leaveStream(id).catch(this.onError);
  }

  #stopWatching() {
    this.watching = null;
    this.#attach(null);
    if (document.fullscreenElement === this.player) document.exitFullscreen().catch(() => {});
  }

  #attach(mediaStream) {
    if (this.attached === mediaStream) return;
    this.attached = mediaStream;
    this.needsTap = false;
    this.video.muted = false;
    this.video.srcObject = mediaStream;
    if (mediaStream) this.video.play().catch(() => this.#playMuted());
  }

  /** Sem gesto recente o celular recusa vídeo com som; melhor mudo que parado. */
  #playMuted() {
    this.video.muted = true;
    this.needsTap = true;
    this.video.play().catch(() => {});
    this.render();
  }

  #unmute() {
    this.video.muted = !this.video.muted;
    this.needsTap = false;
    this.video.play().catch(() => {});
    this.render();
  }

  #toggleFullscreen() {
    if (document.fullscreenElement) return void document.exitFullscreen().catch(() => {});
    if (!this.player.requestFullscreen) return void this.video.webkitEnterFullscreen?.(); // iPhone
    this.player.requestFullscreen().then(() => screen.orientation?.lock?.('landscape')).catch(() => {});
  }

  #renderPlayer(view) {
    this.player.hidden = !view;
    if (!view) return;
    const info = this.handler.streams.get(view.streamIdentifier);
    const playing = view.status === InboundStatus.PLAYING || view.status === InboundStatus.RECONNECTING;
    this.#attach(playing ? view.session?.mediaStream ?? null : null);
    replaceChildren(this.center, this.#playerCenter(view, info));
    const fullscreen = document.fullscreenElement === this.player;
    replaceChildren(this.controls,
      h('div.mobile-now',
        h('strong.ellipsis', info?.name ?? ''),
        h('span.muted.ellipsis', info ? this.handler.clientName(info.ownerClid) : '')),
      playing ? this.#control(this.video.muted ? 'mute' : 'volume', t('video.action.volume'), () => this.#unmute()) : null,
      playing ? this.#control(fullscreen ? 'fullscreenExit' : 'fullscreen',
        t(fullscreen ? 'video.action.fullscreen.close' : 'video.action.fullscreen.open'), () => this.#toggleFullscreen()) : null,
      this.#control('leave', t('video.action.leave'), () => this.#leave(), 'danger'));
  }

  #playerCenter(view, info) {
    const id = view.streamIdentifier;
    if (view.status === InboundStatus.ERROR) {
      return h('div.stream-preview__error',
        h('div.stream-preview__error__text', icon('warning'), t(view.error.key, view.error.params)),
        h('button.btn', { on: { click: () => this.manager.joinStream(id).catch(this.onError) } }, t('video.action.retry')));
    }
    if (view.status === InboundStatus.WAITING || view.status === InboundStatus.CONNECTING) {
      return h('div.status-pill', h('span.spinner'), t(view.status === InboundStatus.WAITING ? 'video.join.status.waiting_for_answer' : 'video.join.status.connecting'));
    }
    if (view.status === InboundStatus.RECONNECTING) return h('div.status-pill', h('span.spinner'), t('video.join.status.reconnecting'));
    if (this.needsTap) return h('button.btn.primary', { on: { click: () => this.#unmute() } }, icon('volume'), t('video.mobile.unmute'));
    if (info?.paused) return h('div.paused-label', icon('pause'), t('video.stream.paused'));
    return null;
  }

  #control(iconName, title, onClick, cls = '') {
    return h('button.mobile-control', { class: cls, title, 'aria-label': title, on: { click: onClick } }, icon(iconName));
  }

  #streams() {
    const own = this.handler.ownClient;
    if (!own) return [];
    return this.handler.streamsInChannel(own.cid)
      .filter(s => !this.manager.ownStreams.has(s.streamIdentifier))
      .sort((a, b) => a.created - b.created);
  }

  #renderList() {
    const streams = this.#streams();
    replaceChildren(this.list,
      streams.length
        ? [h('h2.mobile-list-title', t('video.mobile.live_now')), streams.map(s => this.#card(s))]
        : h('div.stream-empty',
          icon('stream', 'big'),
          h('h3', t('video.stream.empty')),
          h('p', t('video.mobile.empty.hint'))),
      h('p.mobile-note', t('video.mobile.watch_only')));
  }

  #card(s) {
    return h('button.mobile-card', {
      class: { watching: s.streamIdentifier === this.watching },
      on: { click: () => this.#watch(s.streamIdentifier) },
    },
    h('span.mobile-card-thumb', { style: s.thumbnail ? { backgroundImage: `url("${s.thumbnail}")` } : null },
      s.thumbnail ? null : icon(TYPE_ICON[s.type] ?? 'stream'),
      h('span.badge.live', t('video.stream.live'))),
    h('span.mobile-card-text',
      h('strong.ellipsis', s.name),
      h('span.muted.ellipsis', this.handler.clientName(s.ownerClid))),
    h('span.viewer-count', icon('users'), String(s.viewerCount ?? 0)));
  }
}
