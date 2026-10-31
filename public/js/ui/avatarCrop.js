// Ajuste da foto antes do envio: arrastar move, a roda do mouse ou o controle dão zoom.

import { h } from './dom.js';
import { icon } from './icons.js';
import { t } from './i18n.js';

const VIEW = 280;
const OUTPUT = 128; // cabe com folga no limite do servidor (MAX_AVATAR_BYTES)

/** Quadrado de origem (px da imagem) para um centro em frações e um zoom >= 1, sempre dentro da imagem. */
export function cropRect(width, height, { centerX, centerY, zoom }) {
  const side = Math.min(width, height) / zoom;
  const clamp = (value, max) => Math.max(0, Math.min(max, value));
  return { x: clamp(centerX * width - side / 2, width - side), y: clamp(centerY * height - side / 2, height - side), side };
}

function draw(canvas, image, rect) {
  canvas.getContext('2d').drawImage(image, rect.x, rect.y, rect.side, rect.side, 0, 0, canvas.width, canvas.height);
}

/** Abre o ajuste e resolve com a data URL da foto, ou null se cancelar. */
export function adjustAvatar(file) {
  return createImageBitmap(file).then(image => new Promise(resolve => {
    const state = { centerX: 0.5, centerY: 0.5, zoom: 1 };
    const rect = () => cropRect(image.width, image.height, state);
    // Guarda o centro já limitado: arrastar além da borda não acumula.
    const refresh = () => {
      const r = rect();
      state.centerX = (r.x + r.side / 2) / image.width;
      state.centerY = (r.y + r.side / 2) / image.height;
      draw(preview, image, r);
    };
    const preview = h('canvas.avatar-crop', { width: VIEW, height: VIEW });
    const zoom = h('input.range', { type: 'range', min: 1, max: 4, step: 0.01, value: 1, on: {
      input: ev => { state.zoom = Number(ev.target.value); refresh(); },
    } });
    preview.addEventListener('pointerdown', ev => preview.setPointerCapture(ev.pointerId));
    preview.addEventListener('pointermove', ev => {
      if (!preview.hasPointerCapture(ev.pointerId)) return;
      const scale = rect().side / VIEW;
      state.centerX -= ev.movementX * scale / image.width;
      state.centerY -= ev.movementY * scale / image.height;
      refresh();
    });
    preview.addEventListener('wheel', ev => {
      ev.preventDefault();
      state.zoom = Math.max(1, Math.min(4, state.zoom * (ev.deltaY < 0 ? 1.1 : 1 / 1.1)));
      zoom.value = state.zoom;
      refresh();
    }, { passive: false });
    const finish = result => { backdrop.remove(); image.close(); resolve(result); };
    const backdrop = h('div.modal-backdrop',
      h('div.modal.avatar-crop-dialog', { role: 'dialog', 'aria-label': t('app.profile.photo.adjust') },
        h('header.modal-header', icon('user'), h('h2', t('app.profile.photo.adjust'))),
        h('div.avatar-crop-body', preview,
          h('label.field', h('span.field-label', t('app.profile.photo.zoom')), zoom)),
        h('footer.modal-footer',
          h('span.spacer'),
          h('button.btn.ghost', { type: 'button', on: { click: () => finish(null) } }, t('app.profile.photo.cancel')),
          h('button.btn.primary', { type: 'button', on: { click: () => {
            const output = h('canvas', { width: OUTPUT, height: OUTPUT });
            draw(output, image, rect());
            finish(output.toDataURL('image/jpeg', 0.8));
          } } }, t('app.profile.photo.apply')))));
    document.body.append(backdrop);
    refresh();
  }));
}
