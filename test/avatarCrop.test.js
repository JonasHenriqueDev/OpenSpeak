import { test } from 'node:test';
import assert from 'node:assert/strict';
import { cropRect } from '../public/js/ui/avatarCrop.js';

test('sem ajuste, a foto usa o quadrado central', () => {
  assert.deepEqual(cropRect(400, 200, { centerX: 0.5, centerY: 0.5, zoom: 1 }), { x: 100, y: 0, side: 200 });
});

test('o zoom reduz o recorte em volta do centro escolhido', () => {
  assert.deepEqual(cropRect(400, 200, { centerX: 0.25, centerY: 0.5, zoom: 2 }), { x: 50, y: 50, side: 100 });
});

test('o recorte nunca sai da imagem', () => {
  assert.deepEqual(cropRect(400, 200, { centerX: 2, centerY: -1, zoom: 2 }), { x: 300, y: 0, side: 100 });
});
