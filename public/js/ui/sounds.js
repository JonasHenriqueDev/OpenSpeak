// Efeitos sonoros de canal e transmissão (mesma lógica do SoundEffects do app nativo):
// entrar/sair do canal, alguém entrar/sair, streams iniciados/pausados no canal atual
// e espectadores entrando/saindo das minhas transmissões.
// Temas: "suave" (padrão) e "bolha"; arquivos em /sounds/<tema>/<evento>.wav.

export const SOUND_THEMES = ['suave', 'bolha'];
const EVENTS = ['entrar_sala', 'sair_sala', 'alguem_entrou', 'alguem_saiu', 'iniciar_transmissao', 'pausar_transmissao',
  'espectador_entrou', 'espectador_saiu'];

const cache = new Map();

function audio(theme, event) {
  const key = `${SOUND_THEMES.includes(theme) ? theme : SOUND_THEMES[0]}/${event}`;
  if (!cache.has(key)) {
    const el = new Audio(`sounds/${key}.wav`);
    el.preload = 'auto';
    cache.set(key, el);
  }
  return cache.get(key);
}

/** Toca um evento no tema indicado (também usado pela prévia nas configurações). */
export function previewSound(settings, theme, event) {
  if (!EVENTS.includes(event)) return;
  const el = audio(theme, event);
  // volume perceptual: o controle é em %, o elemento é linear
  el.volume = Math.pow(Math.min(100, Math.max(0, settings.get('soundVolume') ?? 60)) / 100, 2);
  el.currentTime = 0;
  el.play().catch(() => { /* sem interação ainda (autoplay bloqueado) */ });
}

export class SoundEffects {
  constructor(handler, settings) {
    this.handler = handler;
    this.settings = settings;
    this.channel = 0;
    this.others = new Set();
    this.streams = new Map(); // streamIdentifier -> pausado
    this.viewers = new Map(); // minhas transmissões -> espectadores
    this.pending = false;
    for (const event of EVENTS) audio(settings.get('soundTheme'), event);
    const schedule = () => {
      if (this.pending) return;
      this.pending = true;
      queueMicrotask(() => { this.pending = false; this.refresh(); });
    };
    for (const ev of ['changed', 'streamadded', 'streamupdated', 'streamremoved']) handler.on(ev, schedule);
    handler.on('disconnected', () => {
      if (this.channel) this.play('sair_sala');
      this.channel = 0;
      this.others.clear();
      this.streams.clear();
      this.viewers.clear();
    });
    this.refresh();
  }

  refresh() {
    const hd = this.handler;
    const channel = hd.ownClient?.cid ?? 0;
    const others = new Set();
    const streams = new Map();
    if (channel) {
      for (const c of hd.clients.values()) if (c.cid === channel && c.clid !== hd.ownClid) others.add(c.clid);
      for (const s of hd.streams.values()) if (s.cid === channel) streams.set(s.streamIdentifier, !!s.paused);
    }
    // espectadores das minhas transmissões (a contagem vem do servidor)
    const viewers = new Map();
    for (const s of hd.streams.values()) if (s.ownerClid === hd.ownClid) viewers.set(s.streamIdentifier, s.viewerCount ?? 0);
    let viewerJoined = false, viewerLeft = false;
    for (const [id, count] of viewers) {
      const before = this.viewers.get(id) ?? 0;
      if (count > before) viewerJoined = true;
      if (count < before) viewerLeft = true;
    }

    let event = null;
    if (channel !== this.channel) {
      // ao trocar de canal, o que já estava lá não toca nada
      if (channel) event = 'entrar_sala';
      else if (this.channel) event = 'sair_sala';
    } else if (channel) {
      let started = false, paused = false;
      for (const [id, isPaused] of streams) {
        const old = this.streams.get(id);
        if (!isPaused && (old === undefined || old)) started = true;
        else if (isPaused && old === false) paused = true;
      }
      for (const [id, wasPaused] of this.streams) if (!wasPaused && !streams.has(id)) paused = true; // encerrado
      // pessoas antes de streams: quem entra já transmitindo toca só "entrou"
      if ([...others].some(c => !this.others.has(c))) event = 'alguem_entrou';
      else if ([...this.others].some(c => !others.has(c))) event = 'alguem_saiu';
      else if (viewerJoined) event = 'espectador_entrou';
      else if (viewerLeft) event = 'espectador_saiu';
      else if (started) event = 'iniciar_transmissao';
      else if (paused) event = 'pausar_transmissao';
    }

    this.channel = channel;
    this.others = others;
    this.streams = streams;
    this.viewers = viewers;
    if (event) this.play(event);
  }

  play(event) {
    if (this.settings.get('soundEffects')) previewSound(this.settings, this.settings.get('soundTheme'), event);
  }
}
