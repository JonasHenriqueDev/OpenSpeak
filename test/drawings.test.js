import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DrawingBoard, videoContentRect, DRAW_MAX_COORDS } from '../public/js/video/drawings.js';

function board(ownClid = 1) {
  const sent = [];
  const timers = [];
  let changes = 0;
  const b = new DrawingBoard({
    ownClid: () => ownClid,
    send: args => sent.push(args),
    onChange: () => changes++,
    setTimer: fn => { timers.push(fn); return timers.length; },
    clearTimer: () => {},
  });
  const tick = () => timers.splice(0).forEach(fn => fn());
  return { b, sent, tick, changes: () => changes };
}

test('desenho: meu traço é juntado em mensagens e termina com end', () => {
  const { b, sent, tick } = board();
  b.drawTo(0, 0, true, '#00FF00');
  b.drawTo(0.5, 0.5, false);
  b.drawTo(0.5001, 0.5, false); // passo pequeno demais: ignorado
  b.drawTo(2, -1, false); // fora do quadro: preso à borda
  assert.equal(sent.length, 0);
  tick();
  assert.deepEqual(sent, [{ action: 'stroke', stroke: 1, color: '#00ff00', width: 5, points: [0, 0, 5000, 5000, 10000, 0], end: false }]);
  b.end();
  assert.deepEqual(sent.at(-1), { action: 'stroke', stroke: 1, color: '#00ff00', width: 5, points: [], end: true });
  b.drawTo(0.1, 0.1, true);
  assert.equal(b.strokes.length, 2);
  assert.equal(b.strokes[1].id, 2);
});

test('desenho: traço longo é enviado antes do limite por mensagem', () => {
  const { b, sent } = board();
  for (let i = 0; i <= DRAW_MAX_COORDS / 2; i++) b.drawTo(i / 500, 0, i === 0);
  assert.equal(sent.length, 1);
  assert.equal(sent[0].points.length, DRAW_MAX_COORDS);
});

test('desenho: traços dos outros, apagar e quem sai do stream', () => {
  const { b, sent } = board(1);
  b.apply({ clid: 2, action: 'stroke', stroke: 5, color: '#ff0000', width: 4, points: [1, 2] });
  b.apply({ clid: 2, action: 'stroke', stroke: 5, color: '#ff0000', width: 4, points: [3, 4], end: true });
  b.apply({ clid: 3, action: 'stroke', stroke: 5, color: '#0000ff', width: 4, points: [9, 9] });
  b.apply({ clid: 1, action: 'stroke', stroke: 9, color: '#0000ff', width: 4, points: [9, 9] }); // eco meu: ignorado
  assert.deepEqual(b.strokes.map(s => [s.clid, s.points]), [[2, [1, 2, 3, 4]], [3, [9, 9]]]);

  b.drawTo(0.5, 0.5, true);
  assert.ok(b.hasMine);
  b.apply({ clid: 2, action: 'clear' });
  assert.deepEqual(b.strokes.map(s => s.clid), [3, 1]);
  b.prune([2]); // o 3 saiu; os meus ficam
  assert.deepEqual(b.strokes.map(s => s.clid), [1]);
  b.clearMine();
  assert.equal(b.strokes.length, 0);
  assert.deepEqual(sent.at(-1), { action: 'clear' });
});

test('desenho: área do vídeo com faixas pretas (object-fit: contain)', () => {
  assert.deepEqual(videoContentRect(200, 100, 1920, 1080), { x: (200 - 1920 * 100 / 1080) / 2, y: 0, w: 1920 * 100 / 1080, h: 100 });
  assert.deepEqual(videoContentRect(100, 100, 200, 100), { x: 0, y: 25, w: 100, h: 50 });
  assert.deepEqual(videoContentRect(100, 80, 0, 0), { x: 0, y: 0, w: 100, h: 80 });
});

test('desenho: cada ponto some em 10 s, o mais velho primeiro', async () => {
  const { fadeLevel } = await import('../public/js/video/drawings.js');
  let now = 0;
  const b = new DrawingBoard({ send: () => {}, now: () => now, setTimer: () => 1, clearTimer: () => {} });
  b.apply({ clid: 2, action: 'stroke', stroke: 1, color: '#ff0000', width: 4, points: [0, 0] });
  now = 3000;
  b.apply({ clid: 2, action: 'stroke', stroke: 1, color: '#ff0000', width: 4, points: [100, 100] });
  assert.equal(fadeLevel(5000), 1);
  assert.ok(fadeLevel(8000) > 0 && fadeLevel(8000) < 1);
  assert.equal(fadeLevel(10_001), 0);
  now = 10_001;
  b.expire();
  assert.deepEqual(b.strokes[0].points, [100, 100]); // o ponto mais velho sumiu
  now = 13_001;
  assert.equal(b.expire().length, 0);
});
