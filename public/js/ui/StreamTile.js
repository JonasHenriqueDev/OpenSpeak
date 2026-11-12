// Um quadro na área de streams. Mantém o <video> vivo entre atualizações;
// só a sobreposição (overlay) é redesenhada.

import { h, replaceChildren } from './dom.js';
import { icon } from './icons.js';
import { t } from './i18n.js';
import { StreamAccess, StreamType, Severity, RELAY_CLID } from '../core/protocol.js';
import { InboundStatus } from '../video/ScreenshareManager.js';
import { installFullscreenIdle } from './fullscreenIdle.js';
import { viewerCount } from './viewerCount.js';
import { DRAW_PALETTE, paintStrokes, videoContentRect } from '../video/drawings.js';

const ACCESS_ICON = { [StreamAccess.PUBLIC]: 'globe', [StreamAccess.CONTACTS_ONLY]: 'contacts', [StreamAccess.PRIVATE]: 'lock' };
const MOVE_THRESHOLD = 4;
const TYPE_ICON = { [StreamType.CAMERA]: 'camera', [StreamType.SCREEN]: 'screen', [StreamType.WINDOW]: 'window' };

function btn(iconName, title, onClick, cls = '') {
  return h('button.tile-btn', {
    class: cls, title, 'aria-label': title,
    on: { click: ev => { ev.stopPropagation(); onClick(ev); } },
  }, icon(iconName));
}

function initials(name) {
  return (name || '?').trim().split(/\s+/).slice(0, 2).map(p => p[0]).join('').toUpperCase();
}

export class StreamTile {
  /**
   * @param {object} actions  callbacks da StreamArea
   */
  constructor(streamIdentifier, actions) {
    this.streamIdentifier = streamIdentifier;
    this.actions = actions;
    this.volume = 1;
    this.muted = false;
    this.video = h('video.tile-video', { autoplay: true, playsInline: true });
    this.overlay = h('div.tile-overlay');
    // Desenhos dos espectadores (STREAM_DRAW): fica vivo entre atualizações, como o vídeo.
    this.canvas = h('canvas.tile-drawing', { hidden: true });
    this.drawMode = false;
    this.drawMenuOpen = false;
    this.el = h('div.video-stream', { dataset: { stream: streamIdentifier } }, this.video, this.canvas, this.overlay,
      this.#grip(true, true), this.#grip(false, true), this.#grip(true, false), this.#grip(false, false));
    this.el.addEventListener('pointerdown', ev => this.#startMove(ev));
    this.el.addEventListener('dblclick', () => {
      if (this.drawMode) return;
      if (this.pipWindow) this.togglePip(); else this.toggleFullscreen();
    });
    this.#installDrawing();
    this._disposeFullscreenIdle = installFullscreenIdle(this.el);
    this._srcObject = null;
    this.pipWindow = null;
    // Ocupa o lugar do quadro na grade enquanto ele está na janela de PiP.
    this.placeholder = h('div.video-stream.pip-placeholder', { on: { click: () => this.togglePip() } },
      icon('pip', 'big'), h('span', t('video.stream.in_pip')),
      h('button.btn.small', t('video.action.pip.close')));
  }

  setRect(rect, focused = false) {
    this.rect = rect;
    const { x, y, w, h: height } = rect;
    for (const el of [this.el, this.placeholder]) {
      Object.assign(el.style, { transform: `translate(${x}px, ${y}px)`, width: `${w}px`, height: `${height}px` });
      el.classList.toggle('focused', !!focused);
      el.classList.toggle('small', height < 160);
    }
  }

  /** Com um stream em foco a organização é a do foco; ajuste manual só na grade. */
  setArrangeable(on) {
    this.el.classList.toggle('arrangeable', on);
  }

  /** Canto que redimensiona o preview. */
  #grip(left, top) {
    const start = ev => {
      if (ev.button !== 0) return;
      ev.stopPropagation();
      const rect = this.actions.beginArrange(this.streamIdentifier);
      this.#track(ev, (dx, dy) => this.actions.resizeTile(this.streamIdentifier, rect, left, top, dx, dy));
    };
    return h('div.tile-grip', { class: `${top ? 'top' : 'bottom'} ${left ? 'left' : 'right'}`, on: { pointerdown: start } });
  }

  /** Arrastar o quadro o move; a folga deixa o duplo clique continuar valendo. */
  #startMove(ev) {
    if (ev.button !== 0 || !this.el.classList.contains('arrangeable') || this.pipWindow || this.drawMode) return;
    if (document.fullscreenElement === this.el || ev.target.closest('button, input, a, .tile-grip')) return;
    let rect = null;
    this.#track(ev, (dx, dy) => {
      if (!rect && Math.hypot(dx, dy) < MOVE_THRESHOLD) return;
      rect ??= this.actions.beginArrange(this.streamIdentifier);
      this.actions.moveTile(this.streamIdentifier, rect, dx, dy);
    });
  }

  /** Acompanha o ponteiro até soltar, sem animar o quadro nesse meio-tempo. */
  #track(ev, onMove) {
    const { clientX, clientY, pointerId } = ev;
    const el = ev.currentTarget;
    el.setPointerCapture(pointerId);
    this.el.classList.add('arranging');
    const move = e => onMove(e.clientX - clientX, e.clientY - clientY);
    const end = () => {
      el.removeEventListener('pointermove', move);
      el.removeEventListener('pointerup', end);
      el.removeEventListener('pointercancel', end);
      this.el.classList.remove('arranging');
    };
    el.addEventListener('pointermove', move);
    el.addEventListener('pointerup', end);
    el.addEventListener('pointercancel', end);
  }

  setHidden(hidden) {
    this.placeholder.hidden = hidden;
    this.el.hidden = hidden && !this.pipWindow;
  }

  toggleFullscreen() {
    if (document.fullscreenElement === this.el) document.exitFullscreen();
    else this.el.requestFullscreen?.().catch(() => {});
  }

  // -------------------------------------------------- picture-in-picture

  static get documentPipSupported() {
    return 'documentPictureInPicture' in window;
  }

  /**
   * Document Picture-in-Picture (Chromium 116+): o próprio quadro, com os
   * controles, vai para uma janela flutuante que o navegador mantém por cima de
   * todos os apps. Sem a API, cai no PiP do <video> (ação "desligar" da Media Session).
   */
  async togglePip() {
    if (this.pipWindow) return this.pipWindow.close();
    if (document.pictureInPictureElement === this.video) return document.exitPictureInPicture().catch(() => {});
    if (StreamTile.documentPipSupported) {
      try { return await this.#openDocumentPip(); } catch (err) { console.warn('[pip]', err); }
    }
    this.#setHangupAction();
    return this.video.requestPictureInPicture?.().catch(() => {});
  }

  async #openDocumentPip() {
    const { videoWidth: vw, videoHeight: vh } = this.video;
    const width = 480;
    const height = vw && vh ? Math.round(width * vh / vw) : 270;
    const win = await window.documentPictureInPicture.requestWindow({ width, height });
    for (const sheet of document.styleSheets) {
      if (sheet.href) {
        win.document.head.append(h('link', { rel: 'stylesheet', href: sheet.href }));
      } else {
        try { win.document.head.append(h('style', [...sheet.cssRules].map(r => r.cssText).join('\n'))); } catch {}
      }
    }
    win.document.title = this.model?.info?.name ?? 'OpenSpeak';
    win.document.body.classList.add('pip-body');
    if (document.fullscreenElement === this.el) await document.exitFullscreen().catch(() => {});
    this.el.replaceWith(this.placeholder);
    this.el.classList.add('in-pip');
    this.el.hidden = false;
    this.pipWindow = win;
    win.document.body.append(this.el);
    this.video.play().catch(() => {});
    win.addEventListener('pagehide', () => this.#restoreFromPip(), { once: true });
    if (this.model) this.update(this.model);
  }

  #restoreFromPip() {
    this.pipWindow = null;
    this.el.classList.remove('in-pip');
    if (this.destroyed) return;
    this.el.hidden = this.placeholder.hidden;
    if (this.placeholder.isConnected) this.placeholder.replaceWith(this.el);
    this.video.play().catch(() => {});
    if (this.model) this.update(this.model);
  }

  #setHangupAction() {
    const a = this.actions;
    const id = this.streamIdentifier;
    try {
      navigator.mediaSession?.setActionHandler('hangup', () => {
        document.exitPictureInPicture().catch(() => {});
        if (this.model?.kind === 'own') a.stopStream(id);
        else a.leaveStream(id);
      });
    } catch {}
  }

  #pipButton() {
    const inPip = !!this.pipWindow;
    return btn('pip', t(inPip ? 'video.action.pip.close' : 'video.action.pip.open'), () => this.togglePip(), inPip ? 'active' : '');
  }

  #attach(mediaStream, { muted }) {
    if (this._srcObject !== mediaStream) {
      this._srcObject = mediaStream;
      this.video.srcObject = mediaStream;
      if (mediaStream) this.video.play().catch(() => {});
    }
    this.video.muted = muted || this.muted;
    this.video.volume = this.volume;
    this.video.hidden = !mediaStream;
  }

  // ------------------------------------------------------------ desenho

  #installDrawing() {
    const c = this.canvas;
    let drawingPointer = null;
    const at = (ev, start) => {
      const box = c.getBoundingClientRect();
      const r = videoContentRect(box.width, box.height, this.video.videoWidth, this.video.videoHeight);
      if (r.w <= 0 || r.h <= 0) return;
      this.actions.drawTo(this.streamIdentifier, (ev.clientX - box.left - r.x) / r.w, (ev.clientY - box.top - r.y) / r.h, start);
    };
    c.addEventListener('pointerdown', ev => {
      if (!this.drawMode || ev.button !== 0) return;
      ev.stopPropagation();
      ev.preventDefault();
      drawingPointer = ev.pointerId;
      c.setPointerCapture(ev.pointerId);
      at(ev, true);
    });
    c.addEventListener('pointermove', ev => {
      if (ev.pointerId !== drawingPointer) return;
      // A lista vem vazia em alguns navegadores e em eventos sintéticos.
      const coalesced = ev.getCoalescedEvents?.();
      for (const e of coalesced?.length ? coalesced : [ev]) at(e, false);
    });
    const end = ev => {
      if (ev.pointerId !== drawingPointer) return;
      drawingPointer = null;
      this.actions.endDrawing(this.streamIdentifier);
    };
    c.addEventListener('pointerup', end);
    c.addEventListener('pointercancel', end);
    this._drawResize = new ResizeObserver(() => this.paintDrawings());
    this._drawResize.observe(this.el);
    this.video.addEventListener('resize', () => this.paintDrawings());
  }

  /** Redesenha os traços (chamado a cada `drawingschanged` do stream). */
  paintDrawings() {
    const c = this.canvas;
    if (c.hidden) return;
    const dpr = (this.pipWindow ?? window).devicePixelRatio || 1;
    const w = Math.round(c.clientWidth * dpr), hgt = Math.round(c.clientHeight * dpr);
    if (c.width !== w || c.height !== hgt) Object.assign(c, { width: w, height: hgt });
    const rect = videoContentRect(w, hgt, this.video.videoWidth, this.video.videoHeight);
    const strokes = this.actions.drawings(this.streamIdentifier);
    paintStrokes(c.getContext('2d'), strokes, rect);
    // Enquanto houver traço, o fade anima a cada quadro.
    if (strokes.length && !this._drawFrame) {
      const win = this.pipWindow ?? window;
      this._drawFrame = win.requestAnimationFrame(() => { this._drawFrame = 0; this.paintDrawings(); });
    }
  }

  #setDrawing(canDraw, show) {
    const visible = canDraw && show;
    if (!canDraw || !show) this.drawMode = false;
    this.canvas.hidden = !visible;
    this.el.classList.toggle('drawing', this.drawMode);
    if (visible) this.paintDrawings();
  }

  #drawButton(id) {
    const a = this.actions;
    const { show } = a.drawSettings();
    const item = (label, onClick) => h('button.draw-menu__item', {
      on: { click: ev => { ev.stopPropagation(); this.drawMenuOpen = false; onClick(); this.update(this.model); } },
    }, label);
    return h('span.draw-wrap',
      btn('pen', t('video.draw'), () => { this.drawMenuOpen = !this.drawMenuOpen; this.update(this.model); },
        this.drawMode ? 'active' : show ? '' : 'dim'),
      this.drawMenuOpen ? h('div.draw-menu', { on: { pointerdown: ev => ev.stopPropagation() } },
        item(t(this.drawMode ? 'video.draw.stop' : 'video.draw.start'), () => {
          if (!this.drawMode && !show) a.setDrawSetting('showDrawings', true);
          this.drawMode = !this.drawMode;
        }),
        item(t(show ? 'video.draw.hide' : 'video.draw.show'), () => {
          if (show) this.drawMode = false;
          a.setDrawSetting('showDrawings', !show);
        }),
        item(t('video.draw.clear_mine'), () => a.clearMyDrawings(id))) : null);
  }

  #drawToolbar(id) {
    if (!this.drawMode) return null;
    const a = this.actions;
    const { color } = a.drawSettings();
    return h('div.draw-toolbar', { on: { pointerdown: ev => ev.stopPropagation(), dblclick: ev => ev.stopPropagation() } },
      ...DRAW_PALETTE.map(c => h('button.draw-swatch', {
        class: c === color ? 'current' : '', title: c, style: { background: c },
        on: { click: ev => { ev.stopPropagation(); a.setDrawSetting('drawColor', c); } },
      })),
      h('span.draw-sep'),
      btn('eraser', t('video.draw.clear_mine'), () => a.clearMyDrawings(id)),
      btn('check', t('video.draw.stop'), () => { this.drawMode = false; this.update(this.model); }, 'active'));
  }

  /**
   * @param {object} m
   * @param {'own'|'remote'} m.kind
   * @param {object} m.info        stream público (handler.streams)
   * @param {object} [m.own]       OwnStream (ScreenshareManager)
   * @param {object} [m.view]      InboundStream (ScreenshareManager)
   * @param {string} m.ownerName
   * @param {number} m.severity
   * @param {boolean} m.isFocused
   */
  update(m) {
    this.model = m;
    const el = this.el;
    el.classList.toggle('own', m.kind === 'own');
    el.classList.toggle('paused', !!m.info?.paused);
    if (m.kind === 'own') return this.#renderOwn(m);
    return this.#renderRemote(m);
  }

  // ------------------------------------------------------------ próprio

  #renderOwn(m) {
    const { own } = m;
    this.#setDrawing(false, false);
    this.#attach(own.source.stream, { muted: true });
    const pending = own.joinRequests.size;
    const a = this.actions;
    const id = own.streamIdentifier;
    replaceChildren(this.overlay,
      this.#topBar(m, h('span.badge.live', t('video.stream.live'))),
      own.paused ? h('div.tile-center', h('div.paused-label', icon('pause'), t('video.stream.paused'))) : null,
      h('div.tile-actions',
        btn(own.paused ? 'play' : 'pause', t(own.paused ? 'video.action.resume' : 'video.action.pause'), () => a.togglePause(id)),
        btn('swap', t('video.action.change_source'), () => a.changeSource(id)),
        btn('settings', t('video.settings'), () => a.editStream(id)),
        h('span.btn-badge-wrap',
          btn('users', t('video.action.manage_viewers'), () => a.manageViewers(id), pending ? 'attention' : ''),
          pending ? h('span.btn-badge', String(pending)) : null),
        btn('chart', t('video.action.stats'), () => a.showStats(id)),
        h('span.spacer'),
        this.pipWindow ? null : btn(m.isFocused ? 'grid' : 'focus', t(m.isFocused ? 'video.action.unfocus' : 'video.action.focus'), () => a.toggleFocus(id)),
        this.#pipButton(),
        this.pipWindow ? null : this.#fullscreenButton(),
        btn('stop', t('video.action.stop_stream'), () => a.stopStream(id), 'danger'),
      ),
    );
  }

  // ------------------------------------------------------------ remoto

  #renderRemote(m) {
    const { info, view } = m;
    const a = this.actions;
    const id = info.streamIdentifier;
    const status = view?.status ?? null;

    if (status === InboundStatus.PLAYING || status === InboundStatus.RECONNECTING) {
      this.#attach(view.session?.mediaStream ?? null, { muted: false });
      const canDraw = a.canDraw();
      this.#setDrawing(canDraw, a.drawSettings().show);
      if (!canDraw) this.drawMenuOpen = false;
      replaceChildren(this.overlay,
        this.#topBar(m),
        this.#drawToolbar(id),
        info.paused ? h('div.tile-center', h('div.paused-label', icon('pause'), t('video.stream.paused'))) : null,
        status === InboundStatus.RECONNECTING
          ? h('div.tile-center', h('div.status-pill', h('span.spinner'), t('video.join.status.reconnecting'))) : null,
        h('div.tile-actions',
          this.#volumeControl(),
          view.peerClid === RELAY_CLID ? this.#qualityControl(view, id) : null,
          canDraw ? this.#drawButton(id) : null,
          btn('chart', t('video.action.stats'), () => a.showStats(id)),
          h('span.spacer'),
          this.pipWindow ? null : btn('eyeOff', t('video.stream.hide_stream'), () => a.hideStream(id)),
          this.pipWindow ? null : btn(m.isFocused ? 'grid' : 'focus', t(m.isFocused ? 'video.action.unfocus' : 'video.action.focus'), () => a.toggleFocus(id)),
          this.#pipButton(),
          this.pipWindow ? null : this.#fullscreenButton(),
          btn('leave', t('video.action.leave'), () => a.leaveStream(id), 'danger'),
        ),
      );
      return;
    }

    this.#attach(null, { muted: true });
    this.#setDrawing(false, false);
    this.drawMenuOpen = false;
    this.pipWindow?.close(); // parou de assistir
    const thumb = info.thumbnail;
    const card = h('div.stream-preview',
      thumb ? h('div.stream-preview__thumb', { style: { backgroundImage: `url("${thumb}")` } }) : null,
      h('div.stream-preview__user-card',
        h('div.avatar', initials(m.ownerName)),
        h('div.stream-preview__name', info.name),
        h('div.stream-preview__meta',
          icon(TYPE_ICON[info.type] ?? 'stream'), m.ownerName,
          h('span.dot-sep', '•'), icon(ACCESS_ICON[info.access] ?? 'globe'),
          h('span.dot-sep', '•'), icon('users'), String(info.viewerCount ?? 0)),
        this.#previewStatus(view, id)),
    );
    replaceChildren(this.overlay, card,
      h('div.tile-actions.always',
        h('span.spacer'),
        btn('eyeOff', t('video.stream.hide_stream'), () => a.hideStream(id))));
  }

  #previewStatus(view, id) {
    const a = this.actions;
    if (!view) {
      return h('button.btn.primary.stream-preview__join', { on: { click: () => a.joinStream(id) } },
        icon('play'), t('video.action.join'));
    }
    if (view.status === InboundStatus.ERROR) {
      return h('div.stream-preview__error',
        h('div.stream-preview__error__text', icon('warning'), t(view.error.key, view.error.params)),
        h('div.row',
          h('button.btn', { on: { click: () => a.joinStream(id) } }, t('video.action.retry')),
          h('button.btn.ghost', { on: { click: () => a.dismiss(id) } }, t('video.action.dismiss'))));
    }
    const key = view.status === InboundStatus.WAITING ? 'video.join.status.waiting_for_answer' : 'video.join.status.connecting';
    return h('div.stream-preview__status',
      h('div.status-pill', h('span.spinner'), t(key)),
      h('button.btn.ghost', { on: { click: () => a.leaveStream(id) } }, t('video.action.join.abort')));
  }

  #fullscreenButton() {
    const on = document.fullscreenElement === this.el;
    return btn(on ? 'fullscreenExit' : 'fullscreen', t(on ? 'video.action.fullscreen.close' : 'video.action.fullscreen.open'), () => this.toggleFullscreen());
  }

  #topBar(m, ...prefix) {
    const { info } = m;
    const sev = m.severity;
    return h('div.tile-top',
      ...prefix,
      h('span.tile-title', icon(TYPE_ICON[info?.type] ?? 'stream'), h('span.ellipsis', info?.name ?? '')),
      m.kind === 'remote' ? h('span.tile-owner', m.ownerName) : null,
      h('span.spacer'),
      sev ? h('span.issue-indicator', {
        class: sev >= Severity.MAJOR ? 'major' : 'minor',
        title: t('video.action.stats'),
        on: { click: ev => { ev.stopPropagation(); this.actions.showStats(info.streamIdentifier); } },
      }, icon(sev >= Severity.MAJOR ? 'warning' : 'info')) : null,
      viewerCount(info?.viewerCount, () => this.actions.viewerNames(this.streamIdentifier)),
    );
  }

  /** Simulcast (SFU): automático, alta ou baixa. */
  #qualityControl(view, id) {
    if (!view.simulcast) {
      return h('select.quality-select', { disabled: true, title: t('video.quality.single.hint'), 'aria-label': t('video.quality') },
        h('option', t('video.quality.single')));
    }
    const option = (value, label) => h('option', { value, selected: view.qualityPreference === value }, label);
    return h('select.quality-select', {
      class: view.layer === 'low' ? 'low' : '',
      title: t('video.quality'), 'aria-label': t('video.quality'),
      on: {
        click: ev => ev.stopPropagation(),
        change: ev => this.actions.setQuality(id, ev.target.value),
      },
    },
    option('auto', t('video.quality.auto')),
    option('high', t('video.quality.high')),
    option('low', t('video.quality.low')));
  }

  #volumeControl() {
    const slider = h('input.volume-slider', {
      type: 'range', min: '0', max: '1', step: '0.05', value: String(this.muted ? 0 : this.volume),
      on: {
        input: ev => {
          this.volume = Number(ev.target.value);
          this.muted = this.volume === 0;
          this.video.volume = this.volume;
          this.video.muted = this.muted;
        },
        click: ev => ev.stopPropagation(),
        // Roda do mouse sobre o slider ajusta o volume em passos de 5%.
        wheel: ev => {
          ev.preventDefault();
          const v = Math.min(1, Math.max(0, Number(slider.value) + (ev.deltaY < 0 ? 0.05 : -0.05)));
          slider.value = String(Math.round(v * 100) / 100);
          slider.dispatchEvent(new Event('input'));
        },
      },
    });
    return h('span.volume',
      btn(this.muted ? 'mute' : 'volume', t('video.action.volume'), () => {
        this.muted = !this.muted;
        this.video.muted = this.muted;
        this.update(this.model);
      }),
      slider);
  }

  destroy() {
    this.destroyed = true;
    this.pipWindow?.close();
    if (document.pictureInPictureElement === this.video) document.exitPictureInPicture().catch(() => {});
    this._disposeFullscreenIdle();
    this._drawResize?.disconnect();
    this.video.srcObject = null;
    this.placeholder.remove();
    this.el.remove();
  }
}
