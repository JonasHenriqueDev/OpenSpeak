// Desenho na transmissão (STREAM_DRAW): traços dos espectadores por cima do vídeo.
// Coordenadas inteiras 0..DRAW_COORD_MAX relativas ao quadro do vídeo, então o
// traço cai no mesmo ponto da imagem em qualquer tamanho de janela. O servidor só
// repassa entre espectadores: quem transmite nunca recebe. Espelho de
// ScreenshareManager::drawTo/onDraw no app nativo.

export const DRAW_COORD_MAX = 10000;
export const DRAW_MAX_COORDS = 512; // por mensagem
export const DRAW_WIDTH = 5; // milésimos da altura do vídeo
export const DRAW_WIDTH_MAX = 40;
export const DRAW_PALETTE = ['#ff3b30', '#ffcc00', '#34c759', '#00c7ff', '#0a84ff', '#ff2d92', '#ffffff', '#000000'];
export const DEFAULT_DRAW_COLOR = DRAW_PALETTE[0];
const MIN_STEP = 12; // ignora movimentos menores (0,12% do quadro)
const MAX_STROKES_PER_AUTHOR = 300; // os mais antigos somem
const MAX_COORDS_PER_STROKE = 4000;
const FLUSH_MS = 40; // junta os pontos do traço em mensagens
// Cada ponto fica inteiro por DRAW_HOLD_MS e some num fade de DRAW_FADE_MS: a parte
// mais velha do traço vai sumindo primeiro e o desenho todo some em 10 s.
export const DRAW_HOLD_MS = 6000;
export const DRAW_FADE_MS = 4000;

/** Opacidade de um ponto pela idade, em degraus de 1/16. */
export function fadeLevel(age) {
  const a = age <= DRAW_HOLD_MS ? 1 : Math.max(0, 1 - (age - DRAW_HOLD_MS) / DRAW_FADE_MS);
  return Math.ceil(a * 16) / 16;
}

const clampCoord = v => Math.min(DRAW_COORD_MAX, Math.max(0, Math.round(v)));

export class DrawingBoard {
  /**
   * @param {(args:object) => void} send manda um `streamdraw` (sem streamIdentifier)
   * @param {() => void} onChange
   */
  constructor({ send, onChange = () => {}, ownClid = () => 0, setTimer = (fn, ms) => setTimeout(fn, ms), clearTimer = id => clearTimeout(id), now = () => Date.now() }) {
    this.now = now;
    this.send = send;
    this.onChange = onChange;
    this.ownClid = ownClid;
    this.setTimer = setTimer;
    this.clearTimer = clearTimer;
    /** @type {{clid:number, id:number, color:string, width:number, points:number[], times:number[]}[]} */
    this.strokes = [];
    this.nextStroke = 1;
    this.current = null; // meu traço em andamento
    this.pending = [];
    this.timer = null;
  }

  get hasMine() {
    const me = this.ownClid();
    return this.strokes.some(s => s.clid === me);
  }

  /** Continua o meu traço até (x, y) em 0..1; `start` começa outro. */
  drawTo(x, y, start, color = DEFAULT_DRAW_COLOR) {
    const px = clampCoord(Math.min(1, Math.max(0, x)) * DRAW_COORD_MAX);
    const py = clampCoord(Math.min(1, Math.max(0, y)) * DRAW_COORD_MAX);
    let stroke = start ? null : this.current;
    if (stroke && !this.strokes.includes(stroke)) stroke = null; // apagado no meio
    if (!stroke) {
      if (this.current) this.end();
      const c = /^#[0-9a-f]{6}$/i.test(color) ? color.toLowerCase() : DEFAULT_DRAW_COLOR;
      stroke = { clid: this.ownClid(), id: this.nextStroke++, color: c, width: DRAW_WIDTH, points: [], times: [] };
      this.strokes.push(stroke);
      this.#cap(stroke.clid);
      this.current = stroke;
    } else if (stroke.points.length >= 2) {
      const lx = stroke.points.at(-2), ly = stroke.points.at(-1);
      if (Math.abs(px - lx) < MIN_STEP && Math.abs(py - ly) < MIN_STEP) return;
    }
    if (stroke.points.length >= MAX_COORDS_PER_STROKE) return;
    stroke.points.push(px, py);
    stroke.times.push(this.now());
    this.pending.push(px, py);
    this.onChange();
    if (this.pending.length >= DRAW_MAX_COORDS) this.#flush(false);
    else this.timer ??= this.setTimer(() => { this.timer = null; this.#flush(false); }, FLUSH_MS);
  }

  end() {
    if (!this.current) return;
    this.#flush(true);
    this.current = null;
  }

  clearMine() {
    const me = this.ownClid();
    this.current = null;
    this.pending = [];
    this.strokes = this.strokes.filter(s => s.clid !== me);
    this.onChange();
    this.send({ action: 'clear' });
  }

  /** `notifystreamdraw` de outro espectador. */
  apply({ clid, action, stroke: id, color, width, points }) {
    if (clid === this.ownClid()) return;
    if (action === 'clear') {
      this.strokes = this.strokes.filter(s => s.clid !== clid);
      this.onChange();
      return;
    }
    if (action !== 'stroke' || !Array.isArray(points)) return;
    let stroke = this.strokes.find(s => s.clid === clid && s.id === id);
    if (!stroke) {
      if (!points.length) return;
      stroke = { clid, id, color: String(color), width: Math.min(DRAW_WIDTH_MAX, Math.max(1, Number(width) || DRAW_WIDTH)), points: [], times: [] };
      this.strokes.push(stroke);
      this.#cap(clid);
    }
    const now = this.now();
    for (let i = 0; i + 1 < points.length && stroke.points.length < MAX_COORDS_PER_STROKE; i += 2) {
      stroke.points.push(clampCoord(points[i]), clampCoord(points[i + 1]));
      stroke.times.push(now);
    }
    this.onChange();
  }

  /** Descarta os pontos que já sumiram (mais de 10 s). Não chama onChange. */
  expire(now = this.now()) {
    const limit = now - DRAW_HOLD_MS - DRAW_FADE_MS;
    for (const s of this.strokes) {
      let drop = 0;
      while (drop < s.times.length && s.times[drop] < limit) drop++;
      if (drop) {
        s.times.splice(0, drop);
        s.points.splice(0, drop * 2);
      }
    }
    this.strokes = this.strokes.filter(s => s.points.length);
    return this.strokes;
  }

  /** Quem saiu do stream leva os desenhos junto. */
  prune(attendees) {
    const keep = new Set(attendees);
    const me = this.ownClid();
    const before = this.strokes.length;
    this.strokes = this.strokes.filter(s => s.clid === me || keep.has(s.clid));
    if (this.strokes.length !== before) this.onChange();
  }

  dispose() {
    if (this.timer) this.clearTimer(this.timer);
    this.timer = null;
    this.strokes = [];
    this.current = null;
    this.pending = [];
  }

  #flush(end) {
    const stroke = this.current;
    if (!stroke || (!this.pending.length && !end)) return;
    if (!this.strokes.includes(stroke)) { this.pending = []; return; }
    const points = this.pending;
    this.pending = [];
    this.send({ action: 'stroke', stroke: stroke.id, color: stroke.color, width: stroke.width, points, end });
  }

  #cap(clid) {
    let count = this.strokes.filter(s => s.clid === clid).length;
    if (count <= MAX_STROKES_PER_AUTHOR) return;
    this.strokes = this.strokes.filter(s => !(s.clid === clid && count-- > MAX_STROKES_PER_AUTHOR));
  }
}

/** Área do vídeo dentro do elemento (object-fit: contain). */
export function videoContentRect(width, height, videoWidth, videoHeight) {
  if (!videoWidth || !videoHeight) return { x: 0, y: 0, w: width, h: height };
  const scale = Math.min(width / videoWidth, height / videoHeight);
  const w = videoWidth * scale, h = videoHeight * scale;
  return { x: (width - w) / 2, y: (height - h) / 2, w, h };
}

/** Desenha os traços num canvas 2D; `rect` em pixels do canvas. */
export function paintStrokes(ctx, strokes, rect, now = Date.now()) {
  ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
  if (rect.w <= 0 || rect.h <= 0) return;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  const X = v => rect.x + v / DRAW_COORD_MAX * rect.w;
  const Y = v => rect.y + v / DRAW_COORD_MAX * rect.h;
  for (const s of strokes) {
    const p = s.points;
    if (p.length < 2) continue;
    const level = i => fadeLevel(now - (s.times?.[i] ?? now));
    const w = Math.max(2.5, s.width / 1000 * rect.h);
    ctx.strokeStyle = ctx.fillStyle = s.color;
    ctx.lineWidth = w;
    if (p.length === 2) {
      ctx.globalAlpha = level(0);
      ctx.beginPath();
      ctx.arc(X(p[0]), Y(p[1]), w / 2, 0, 2 * Math.PI);
      ctx.fill();
      continue;
    }
    // Trechos vizinhos com o mesmo degrau de opacidade saem num caminho só
    // (sem as emendas mais escuras que o fade faria trecho a trecho).
    const n = p.length / 2;
    for (let i = 0; i < n - 1;) {
      const a = level(i);
      let j = i + 1;
      while (j < n - 1 && level(j) === a) j++;
      if (a > 0) {
        ctx.globalAlpha = a;
        ctx.beginPath();
        ctx.moveTo(X(p[2 * i]), Y(p[2 * i + 1]));
        for (let k = i + 1; k <= j; k++) ctx.lineTo(X(p[2 * k]), Y(p[2 * k + 1]));
        ctx.stroke();
      }
      i = j;
    }
  }
  ctx.globalAlpha = 1;
}
