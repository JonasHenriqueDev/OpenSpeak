// Layouts da área de streams (mesma estratégia observada no TS 6, §5 da análise):
//  - grade automática: maximiza a área coberta mantendo 16:9 e altura mínima;
//    se nem todos cabem, pagina mostrando o máximo possível;
//  - foco: um stream grande e uma faixa de miniaturas abaixo.

export const ASPECT = 16 / 9;
export const MIN_TILE_HEIGHT = 100;
const STRIP_HEIGHT = 100;
export const MIN_CUSTOM_WIDTH = 160;

function fit(count, rows, width, height, gap) {
  const cols = Math.ceil(count / rows);
  const cellW = (width - (cols - 1) * gap) / cols;
  const cellH = (height - (rows - 1) * gap) / rows;
  const w = Math.floor(Math.min(cellW, cellH * ASPECT));
  const h = Math.floor(w / ASPECT);
  return { rows, cols, w, h };
}

function place(count, cfg, width, height, gap) {
  const tiles = [];
  const usedH = cfg.rows * cfg.h + (cfg.rows - 1) * gap;
  const top = Math.max(0, (height - usedH) / 2);
  for (let i = 0; i < count; i++) {
    const row = Math.floor(i / cfg.cols);
    const inRow = row === cfg.rows - 1 ? count - cfg.cols * (cfg.rows - 1) : cfg.cols;
    const col = i % cfg.cols;
    const rowW = inRow * cfg.w + (inRow - 1) * gap;
    tiles.push({
      x: Math.round((width - rowW) / 2 + col * (cfg.w + gap)),
      y: Math.round(top + row * (cfg.h + gap)),
      w: cfg.w,
      h: cfg.h,
    });
  }
  return tiles;
}

/**
 * @param {number} count
 * @param {number} width
 * @param {number} height
 * @param {{gap?:number, offset?:number, minHeight?:number}} [o]
 * @returns {{tiles:{x:number,y:number,w:number,h:number,index:number}[], perPage:number}}
 */
export function gridLayout(count, width, height, { gap = 8, offset = 0, minHeight = MIN_TILE_HEIGHT } = {}) {
  if (count <= 0 || width <= 0 || height <= 0) return { tiles: [], perPage: 0 };
  let best = null;
  for (let rows = 1; rows <= count; rows++) {
    const cfg = fit(count, rows, width, height, gap);
    if (cfg.h < minHeight) continue;
    const area = cfg.w * cfg.h;
    if (!best || area > best.w * best.h) best = cfg;
  }
  let shown = count;
  if (!best) {
    // Paginação: maior quantidade que cabe com a altura mínima.
    const maxRows = Math.max(1, Math.floor((height + gap) / (minHeight + gap)));
    const maxCols = Math.max(1, Math.floor((width + gap) / (minHeight * ASPECT + gap)));
    shown = Math.max(1, Math.min(count, maxRows * maxCols));
    best = null;
    for (let rows = 1; rows <= maxRows; rows++) {
      const cfg = fit(shown, rows, width, height, gap);
      if (cfg.cols > maxCols) continue;
      if (!best || cfg.w * cfg.h > best.w * best.h) best = cfg;
    }
    best ??= fit(shown, 1, width, height, gap);
  }
  const start = Math.min(Math.max(0, offset), Math.max(0, count - shown));
  const tiles = place(Math.min(shown, count - start), best, width, height, gap)
    .map((t, i) => ({ ...t, index: start + i }));
  return { tiles, perPage: shown };
}

/**
 * Layout com foco: `focusIndex` grande, demais numa faixa inferior.
 */
export function focusLayout(count, focusIndex, width, height, { gap = 8, offset = 0 } = {}) {
  if (count <= 0) return { tiles: [], perPage: 0 };
  const others = [];
  for (let i = 0; i < count; i++) if (i !== focusIndex) others.push(i);
  const hasStrip = others.length > 0 && height - STRIP_HEIGHT - gap >= 140;
  const mainH = hasStrip ? height - STRIP_HEIGHT - gap : height;
  let w = Math.floor(Math.min(width, mainH * ASPECT));
  let h = Math.floor(w / ASPECT);
  const tiles = [{ x: Math.round((width - w) / 2), y: 0, w, h, index: focusIndex, focused: true }];
  if (!hasStrip) return { tiles, perPage: 0 };
  const sh = STRIP_HEIGHT;
  const sw = Math.floor(sh * ASPECT);
  const perPage = Math.max(1, Math.floor((width + gap) / (sw + gap)));
  const start = Math.min(Math.max(0, offset), Math.max(0, others.length - perPage));
  const visible = others.slice(start, start + perPage);
  const rowW = visible.length * sw + (visible.length - 1) * gap;
  visible.forEach((index, i) => {
    tiles.push({ x: Math.round((width - rowW) / 2 + i * (sw + gap)), y: h + gap, w: sw, h: sh, index });
  });
  return { tiles, perPage };
}

/**
 * Redimensiona um preview por um canto, mantendo a proporção e o canto oposto
 * no lugar. `left`/`top` dizem qual canto está sendo arrastado.
 */
export function resizeRect(rect, left, top, dx, dy, width, height, minWidth = MIN_CUSTOM_WIDTH) {
  const aspect = rect.w / rect.h;
  const anchorX = left ? rect.x + rect.w : rect.x;
  const anchorY = top ? rect.y + rect.h : rect.y;
  // Vale o eixo que mais se afastou do tamanho inicial.
  const byWidth = rect.w + (left ? -dx : dx);
  const byHeight = (rect.h + (top ? -dy : dy)) * aspect;
  const wanted = Math.abs(byWidth - rect.w) >= Math.abs(byHeight - rect.w) ? byWidth : byHeight;
  const room = Math.min(left ? anchorX : width - anchorX, (top ? anchorY : height - anchorY) * aspect);
  const w = Math.min(Math.max(wanted, minWidth), room);
  const h = w / aspect;
  return { x: left ? anchorX - w : anchorX, y: top ? anchorY - h : anchorY, w, h };
}

/** Move um preview sem deixá-lo sair da área. */
export function moveRect(rect, dx, dy, width, height) {
  return {
    ...rect,
    x: Math.min(Math.max(0, rect.x + dx), width - rect.w),
    y: Math.min(Math.max(0, rect.y + dy), height - rect.h),
  };
}

/** Guarda um preview em frações da área, para acompanhar o tamanho da janela. */
export function rectToFraction(rect, width, height) {
  return { x: rect.x / width, y: rect.y / height, w: rect.w / width, aspect: rect.w / rect.h };
}

/** Volta às coordenadas da área atual, sempre inteiro dentro dela. */
export function rectFromFraction(f, width, height) {
  const w = Math.min(f.w * width, width, height * f.aspect);
  const h = w / f.aspect;
  return {
    x: Math.min(Math.max(0, f.x * width), width - w),
    y: Math.min(Math.max(0, f.y * height), height - h),
    w, h,
  };
}
