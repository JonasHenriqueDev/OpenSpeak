// Layouts da área de streams (mesma estratégia observada no TS 6, §5 da análise):
//  - grade automática: maximiza a área coberta mantendo 16:9 e altura mínima;
//    se nem todos cabem, pagina mostrando o máximo possível;
//  - foco: um stream grande e uma faixa de miniaturas abaixo.

export const ASPECT = 16 / 9;
export const MIN_TILE_HEIGHT = 100;
const STRIP_HEIGHT = 100;
export const MIN_CUSTOM_WIDTH = 160;
const EPSILON = 0.01;

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

const overlapping = (a, b, gap) => a.x < b.x + b.w + gap - EPSILON && b.x < a.x + a.w + gap - EPSILON
  && a.y < b.y + b.h + gap - EPSILON && b.y < a.y + a.h + gap - EPSILON;

/** Menor deslocamento que tira `mover` de cima de `other` sem sair da área. */
function pushAway(mover, other, width, height, gap) {
  const options = [
    { x: other.x - gap - mover.w, y: mover.y },
    { x: other.x + other.w + gap, y: mover.y },
    { x: mover.x, y: other.y - gap - mover.h },
    { x: mover.x, y: other.y + other.h + gap },
  ].filter(p => p.x >= 0 && p.y >= 0 && p.x + mover.w <= width && p.y + mover.h <= height);
  if (!options.length) return false;
  const cost = p => Math.abs(p.x - mover.x) + Math.abs(p.y - mover.y);
  const best = options.reduce((a, b) => (cost(b) < cost(a) ? b : a));
  mover.x = best.x;
  mover.y = best.y;
  return true;
}

function settle(rects, fixed, width, height, gap) {
  for (let pass = 0; pass < 32; pass++) {
    let clear = true;
    for (let i = 0; i < rects.length; i++) {
      for (let j = i + 1; j < rects.length; j++) {
        if (!overlapping(rects[i], rects[j], gap)) continue;
        clear = false;
        const moverIsI = j === fixed;
        pushAway(moverIsI ? rects[i] : rects[j], moverIsI ? rects[j] : rects[i], width, height, gap);
      }
    }
    if (clear) return true;
  }
  return false;
}

/**
 * Tira os previews de cima uns dos outros: empurra os demais e, se não houver
 * espaço, encolhe-os aos poucos. `fixed` é o índice do preview que está sendo
 * arrastado, que não sai do lugar (-1 para nenhum).
 * @returns {{x:number,y:number,w:number,h:number}[]|null} null se não couberem
 */
export function resolveOverlaps(rects, fixed, width, height, gap = 8, minWidth = MIN_CUSTOM_WIDTH) {
  const out = rects.map(r => ({ ...r }));
  for (let attempt = 0; attempt < 40; attempt++) {
    if (settle(out, fixed, width, height, gap)) return out;
    let shrunk = false;
    out.forEach((r, i) => {
      if (i === fixed || r.w <= minWidth) return;
      const w = Math.max(minWidth, r.w * 0.94);
      const h = r.h * w / r.w;
      r.x += (r.w - w) / 2;
      r.y += (r.h - h) / 2;
      r.w = w;
      r.h = h;
      shrunk = true;
    });
    if (!shrunk) return null;
  }
  return null;
}

/** Quem começa antes da metade do primeiro da faixa está na mesma linha: esquerda para a direita. */
function readingOrder(rects) {
  const byTop = rects.map((_, i) => i).sort((a, b) => rects[a].y - rects[b].y);
  const order = [];
  while (byTop.length) {
    const first = rects[byTop[0]];
    const band = byTop.filter(i => rects[i].y < first.y + first.h / 2);
    band.forEach(i => byTop.splice(byTop.indexOf(i), 1));
    order.push(...band.sort((a, b) => rects[a].x - rects[b].x));
  }
  return order;
}

/**
 * Organiza os previews em fileiras alinhadas e centralizadas, mantendo o tamanho
 * de cada um e a ordem de leitura atual. Só encolhe, todos na mesma proporção,
 * se não couberem na área.
 * @returns {{x:number,y:number,w:number,h:number}[]|null} null se não couberem
 */
export function packRects(rects, width, height, gap = 8, minWidth = MIN_CUSTOM_WIDTH) {
  const order = readingOrder(rects);
  for (let scale = 1, attempt = 0; attempt < 60; attempt++, scale *= 0.95) {
    const rows = [];
    for (const index of order) {
      const r = rects[index];
      const w = Math.max(minWidth, r.w * scale);
      const h = r.h * w / r.w;
      const row = rows.at(-1);
      if (row && row.w + gap + w <= width) {
        row.items.push({ index, w, h });
        row.w += gap + w;
        row.h = Math.max(row.h, h);
      } else {
        rows.push({ items: [{ index, w, h }], w, h });
      }
    }
    const totalH = rows.reduce((sum, row) => sum + row.h, 0) + (rows.length - 1) * gap;
    if (totalH > height || rows.some(row => row.w > width)) continue;
    const out = [];
    let y = (height - totalH) / 2;
    for (const row of rows) {
      let x = (width - row.w) / 2;
      for (const item of row.items) {
        out[item.index] = { x, y: y + (row.h - item.h) / 2, w: item.w, h: item.h };
        x += item.w + gap;
      }
      y += row.h + gap;
    }
    return out;
  }
  return null;
}
