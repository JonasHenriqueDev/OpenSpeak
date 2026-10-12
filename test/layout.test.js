import { test } from 'node:test';
import assert from 'node:assert/strict';
import { gridLayout, focusLayout, MIN_TILE_HEIGHT } from '../public/js/video/layout.js';

const overlaps = (a, b) => a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;

test('grade: um stream ocupa a maior área 16:9 possível', () => {
  const { tiles } = gridLayout(1, 1600, 900);
  assert.equal(tiles.length, 1);
  assert.equal(tiles[0].w, 1600);
  assert.equal(tiles[0].h, 900);
});

test('grade: quatro streams viram 2x2 numa tela 16:9', () => {
  const { tiles, perPage } = gridLayout(4, 1608, 908, { gap: 8 });
  assert.equal(perPage, 4);
  assert.equal(new Set(tiles.map(t => t.y)).size, 2);
  for (let i = 0; i < tiles.length; i++) {
    for (let j = i + 1; j < tiles.length; j++) assert.ok(!overlaps(tiles[i], tiles[j]));
  }
});

test('grade: tudo cabe dentro da área e respeita a altura mínima', () => {
  for (const n of [2, 3, 5, 7, 12]) {
    const { tiles } = gridLayout(n, 1200, 700);
    for (const t of tiles) {
      assert.ok(t.x >= 0 && t.y >= 0 && t.x + t.w <= 1200 && t.y + t.h <= 700, `n=${n}`);
      assert.ok(t.h >= MIN_TILE_HEIGHT);
    }
  }
});

test('grade: pagina quando não cabe', () => {
  const { tiles, perPage } = gridLayout(50, 600, 300);
  assert.ok(perPage < 50);
  assert.equal(tiles.length, perPage);
  const page2 = gridLayout(50, 600, 300, { offset: perPage });
  assert.equal(page2.tiles[0].index, perPage);
});

test('foco: destaque em cima e faixa abaixo', () => {
  const { tiles } = focusLayout(4, 2, 1280, 800);
  const main = tiles.find(t => t.focused);
  assert.equal(main.index, 2);
  const strip = tiles.filter(t => !t.focused);
  assert.equal(strip.length, 3);
  for (const t of strip) assert.ok(t.y >= main.y + main.h);
});
