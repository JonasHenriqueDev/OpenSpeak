// Atividade do Discord: só assistir. Sem WebRTC no Discord, a mídia vem do relay
// SFU pelo próprio WebSocket de sinalização (joinstreamrequest com
// transport "websocket") e é decodificada com WebCodecs (WsVideoPlayer).

import { ConnectionHandler } from '../core/ConnectionHandler.js';
import { LeaveReason, RELAY_CLID, StreamMode } from '../core/protocol.js';
import { h, replaceChildren } from '../ui/dom.js';
import { icon } from '../ui/icons.js';
import { settings } from '../ui/settings.js';
import { DiscordRpc, discordContext, discordNickname } from './discord.js';
import { WsAudioPlayer } from './WsAudioPlayer.js';
import { WsVideoPlayer } from './WsVideoPlayer.js';

const discord = discordContext();
const rpc = discord?.clientId ? new DiscordRpc(discord) : null;
// Login com a conta do Discord (OAuth2): desligado por padrão; liga com
// discordLogin: true no config.js e as credenciais no servidor.
const discordLogin = !!rpc && globalThis.OpenSpeakConfig?.discordLogin === true;
let changingName = false;

const app = document.getElementById('app');
const handler = new ConnectionHandler();
let watching = null; // { streamIdentifier, player, el, idleTimer }

const LEAVE_MESSAGES = {
  [LeaveReason.DENIED]: 'O transmissor recusou o pedido.',
  [LeaveReason.FAILED]: 'O servidor não conseguiu enviar a transmissão.',
  [LeaveReason.KICKED]: 'Você foi removido da transmissão.',
  [LeaveReason.BANNED]: 'Você foi banido desta transmissão.',
  [LeaveReason.STREAM_STOPPED]: 'A transmissão terminou.',
  [LeaveReason.VIEWER_LIMIT]: 'A transmissão atingiu o limite de espectadores.',
};

function serverUrl() {
  const configured = globalThis.OpenSpeakConfig?.serverUrl;
  if (configured && !discord) return configured;
  // No Discord tudo passa pelo proxy da própria origem (URL mapping "/" -> servidor).
  const url = new URL('ws', location.href);
  url.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  url.search = '';
  return url.href;
}

// ------------------------------------------------------------------ entrada

function renderLogin(error = '') {
  const nick = h('input.input', { value: settings.get('nickname'), placeholder: 'Seu nome', maxLength: 32, required: true });
  const button = h('button.btn.primary', { type: 'submit' }, 'Entrar');
  const form = h('form.act-card',
    h('div.act-brand', icon('logo'), h('span', 'OpenSpeak')),
    h('p.act-dim', 'Assista às transmissões do servidor OpenSpeak sem sair do Discord.'),
    h('label.act-field', h('span', 'Nome'), nick),
    error ? h('p.act-error', error) : null,
    button,
    h('p.act-faint', 'Somente para assistir. Para transmitir, use o app do Windows ou a página web.'),
  );
  form.addEventListener('submit', async ev => {
    ev.preventDefault();
    const nickname = nick.value.trim();
    if (!nickname) return;
    settings.set('nickname', nickname);
    button.disabled = true;
    button.textContent = 'Conectando…';
    try {
      await handler.connect(serverUrl(), { nickname, uid: settings.get('uid') });
      renderLobby();
    } catch (err) {
      renderLogin(err?.message || 'Não foi possível conectar ao servidor.');
    }
  });
  replaceChildren(app, h('div.act-center', form));
  nick.focus();
}

// ------------------------------------------------------------------- lobby

let lobbyNotice = '';

function renderLobby() {
  if (watching) return;
  const sfu = handler.hasStreamingSFU;
  const channels = [...handler.channels.values()].sort((a, b) => (a.order ?? 0) - (b.order ?? 0));
  const groups = channels.map(ch => {
    const streams = handler.streamsInChannel(ch.cid);
    if (!streams.length) return null;
    return h('section.act-group',
      h('h2', icon('channel'), ch.name),
      h('div.act-grid', streams.map(s => streamCard(s))));
  }).filter(Boolean);

  replaceChildren(app, h('div.act-lobby',
    h('header.act-top',
      h('div.act-brand', icon('logo'), h('span', handler.serverName || 'OpenSpeak')),
      h('div.act-me',
        h('span.act-dim', handler.ownClient?.nickname ?? ''),
        h('button.btn.small.ghost', { on: { click: changeName } }, 'Trocar nome')),
    ),
    lobbyNotice ? h('div.act-notice', icon('info'), lobbyNotice) : null,
    !sfu ? h('div.act-notice.warn', icon('warning'), 'Este servidor não tem o modo Servidor (SFU); não é possível assistir pelo Discord.') : null,
    groups.length ? groups : h('div.act-empty', icon('stream'), h('p', 'Ninguém está transmitindo agora.'), h('p.act-faint', 'A lista atualiza sozinha.')),
  ));
}

function streamCard(s) {
  const watchable = s.mode === StreamMode.SFU && handler.hasStreamingSFU;
  const owner = handler.clientName(s.ownerClid);
  // Prévia: a miniatura que o transmissor atualiza periodicamente (streamthumbnail).
  const thumb = s.thumbnail ? h('img.act-thumb', { src: s.thumbnail, alt: '' }) : h('div.act-thumb.empty', icon('screen'));
  const button = h('button.btn.primary', { disabled: !watchable }, icon('play'), 'Assistir');
  return h('article.act-stream', {
    class: { disabled: !watchable },
    on: { click: () => watchable && !button.disabled && watch(s, button) },
  },
    h('div.act-preview',
      thumb,
      h('span.act-live', 'AO VIVO'),
      h('span.act-viewers', icon('users'), String(s.viewerCount ?? 0)),
      s.paused ? h('span.act-paused', icon('pause'), 'Pausada') : null,
      watchable ? h('div.act-play', icon('play')) : null,
    ),
    h('div.act-stream-info',
      h('div.act-stream-text',
        h('strong', s.name || 'Transmissão'),
        h('span.act-dim', owner),
      ),
      button,
    ),
    !watchable ? h('p.act-faint.act-stream-note', 'Transmissão P2P: só dá para assistir no app ou na página web.') : null,
  );
}

// ---------------------------------------------------------------- assistir

async function watch(stream, button) {
  if (!WsVideoPlayer.supported) {
    lobbyNotice = 'Este Discord não tem WebCodecs para decodificar o vídeo. Atualize o aplicativo.';
    return renderLobby();
  }
  button.disabled = true;
  button.textContent = 'Entrando…';
  const id = stream.streamIdentifier;
  // O AudioContext precisa nascer dentro do clique (política de autoplay).
  const audio = new WsAudioPlayer();
  audio.unlock();
  try {
    if (handler.ownClient?.cid !== stream.cid) await handler.moveTo(stream.cid);
    openPlayer(stream, audio);
    await handler.connection.sendCommand('joinstreamrequest', { streamIdentifier: id, codecs: ['video/H264'], transport: 'websocket' });
    setStatus('Aguardando o transmissor aceitar…');
  } catch (err) {
    closePlayer(`Não foi possível assistir: ${err?.message ?? err}`);
  }
}

function openPlayer(stream, audio) {
  const canvas = h('canvas.act-canvas');
  const status = h('div.act-status', 'Conectando…');
  const stats = h('span.act-stats');
  const volumeBtn = h('button.act-icon', { title: 'Volume', on: { click: () => { audio.toggleMute(); syncVolume(); } } });
  const volume = h('input.act-volume', {
    type: 'range', min: '0', max: '1', step: '0.05', value: '1', title: 'Volume',
    on: { input: ev => { audio.setVolume(Number(ev.target.value)); syncVolume(); } },
  });
  const syncVolume = () => {
    const muted = audio.muted || audio.volume === 0;
    replaceChildren(volumeBtn, icon(muted ? 'mute' : 'volume'));
    volumeBtn.title = muted ? 'Ativar som' : 'Silenciar';
    volume.value = String(muted ? 0 : audio.volume);
  };
  syncVolume();
  const fsBtn = h('button.act-icon', { title: 'Tela cheia', on: { click: () => toggleFullscreen() } }, icon('fullscreen'));
  let fill = readFill();
  const fillBtn = h('button.act-icon', { on: { click: () => { fill = !fill; saveFill(fill); syncFill(); fit(); } } });
  const syncFill = () => {
    replaceChildren(fillBtn, icon(fill ? 'grid' : 'focus'));
    fillBtn.title = fill ? 'Mostrar a imagem inteira' : 'Preencher a área (corta as bordas)';
  };
  syncFill();
  // Encaixe calculado aqui (e não por object-fit): imagem inteira com faixas
  // onde a proporção não bate, ou, preenchendo, cortando as bordas.
  const fit = () => {
    const { width: w, height: hgt } = canvas;
    const W = el.clientWidth, H = el.clientHeight;
    if (!w || !hgt || !W || !H) return;
    const scale = fill ? Math.max(W / w, H / hgt) : Math.min(W / w, H / hgt);
    canvas.style.width = `${Math.round(w * scale)}px`;
    canvas.style.height = `${Math.round(hgt * scale)}px`;
  };
  const bar = h('div.act-bar',
    h('button.act-icon', { title: 'Voltar', on: { click: () => leave() } }, icon('chevronLeft')),
    h('div.act-title', h('strong', stream.name || 'Transmissão'), h('span.act-dim', handler.clientName(stream.ownerClid))),
    stats,
    WsAudioPlayer.supported ? h('div.act-volume-wrap', volumeBtn, volume) : null,
    fillBtn,
    fsBtn,
  );
  const el = h('div.act-player', canvas, status, bar);
  el.addEventListener('dblclick', () => toggleFullscreen());
  const resizeObserver = new ResizeObserver(fit);
  resizeObserver.observe(el);
  const player = new WsVideoPlayer({
    canvas,
    onAudio: packet => audio.push(packet),
    onResize: fit,
    requestKeyFrame: () => handler.connection.sendCommand('streamsignaling', {
      streamIdentifier: stream.streamIdentifier, clid: RELAY_CLID, payload: { type: 'keyframe' },
    }).catch(() => {}),
  });
  Object.assign(globalThis, { openspeakPlayer: player, openspeakAudio: audio }); // inspeção pelo console
  watching = { streamIdentifier: stream.streamIdentifier, player, audio, resizeObserver, el, status, stats, fsBtn, accepted: false, idleTimer: 0, statsTimer: 0 };
  const wake = () => {
    el.classList.remove('idle');
    clearTimeout(watching?.idleTimer);
    if (watching) watching.idleTimer = setTimeout(() => el.classList.add('idle'), 3000);
  };
  el.addEventListener('pointermove', wake);
  el.addEventListener('pointerdown', wake);
  wake();
  watching.statsTimer = setInterval(() => {
    const st = player.stats;
    stats.textContent = st.frames ? `${st.width}×${st.height} · ${st.fps} fps · ${(st.kbps / 1000).toFixed(1)} Mbps` : '';
    if (st.decoded) status.hidden = true;
    // Com som, a imagem espera o buffer de áudio (lip-sync); sem som, sai na hora.
    player.delayMs = audio.active ? audio.latencyMs : 0;
  }, 1000);
  replaceChildren(app, el);
}

function readFill() {
  try { return localStorage.getItem('openspeak.activity.fill') === '1'; } catch { return false; }
}

function saveFill(on) {
  try { localStorage.setItem('openspeak.activity.fill', on ? '1' : '0'); } catch {}
}

function setStatus(text) {
  if (!watching) return;
  watching.status.hidden = !text;
  watching.status.textContent = text;
}

function toggleFullscreen() {
  if (!watching) return;
  if (document.fullscreenElement) return void document.exitFullscreen().catch(() => {});
  // Dentro do Discord o iframe pode não liberar a Fullscreen API: o player já
  // ocupa a atividade inteira, e o Discord tem o próprio botão de tela cheia.
  watching.el.requestFullscreen?.().catch(() => {
    setStatus('Use o botão de tela cheia do Discord para ampliar a atividade.');
    setTimeout(() => watching?.player.stats.frames && setStatus(''), 3000);
  });
}

function closePlayer(notice = '') {
  if (watching) {
    clearTimeout(watching.idleTimer);
    clearInterval(watching.statsTimer);
    watching.player.dispose();
    watching.audio.dispose();
    watching.resizeObserver.disconnect();
    watching = null;
  }
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
  lobbyNotice = notice;
  renderLobby();
}

function leave() {
  const id = watching?.streamIdentifier;
  closePlayer();
  if (id) handler.connection.sendCommand('removeclientfromstream', { streamIdentifier: id }).catch(() => {});
}

// -------------------------------------------------------------- notificações

const c = handler.connection;
c.on('binary', data => watching?.accepted && watching.player.push(data));
c.on('notifyrespondjoinstreamrequest', ({ streamIdentifier, accepted, transport, reason }) => {
  if (watching?.streamIdentifier !== streamIdentifier) return;
  if (!accepted) return closePlayer(LEAVE_MESSAGES[reason] ?? 'O pedido foi recusado.');
  if (transport !== 'websocket') {
    handler.connection.sendCommand('removeclientfromstream', { streamIdentifier }).catch(() => {});
    return closePlayer('O servidor não suporta assistir por WebSocket.');
  }
  watching.accepted = true;
  setStatus('Aguardando o primeiro quadro…');
});
c.on('notifystreamclientleft', ({ streamIdentifier, clid, reason }) => {
  if (watching?.streamIdentifier === streamIdentifier && clid === handler.ownClid) closePlayer(LEAVE_MESSAGES[reason] ?? '');
});
handler.on('streamremoved', stream => {
  if (watching?.streamIdentifier === stream.streamIdentifier) closePlayer(LEAVE_MESSAGES[LeaveReason.STREAM_STOPPED]);
});
handler.on('changed', () => { if (!watching && handler.connection.connected) renderLobby(); });
handler.on('streamupdated', () => { if (!watching && handler.connection.connected) renderLobby(); }); // miniaturas
handler.on('disconnected', () => {
  closePlayer();
  renderLogin(changingName ? '' : 'A conexão com o servidor caiu.');
  changingName = false;
});

/** Sai do servidor e volta ao formulário para entrar com outro nome. */
function changeName() {
  changingName = true;
  handler.disconnect();
}
document.addEventListener('fullscreenchange', () => {
  if (!watching) return;
  const on = !!document.fullscreenElement;
  replaceChildren(watching.fsBtn, icon(on ? 'fullscreenExit' : 'fullscreen'));
  watching.fsBtn.title = on ? 'Sair da tela cheia' : 'Tela cheia';
});

async function start() {
  let nickname = settings.get('nickname');
  if (discordLogin) {
    replaceChildren(app, h('div.act-center', h('div.act-card',
      h('div.act-brand', icon('logo'), h('span', 'OpenSpeak')),
      h('p.act-dim', 'Entrando com sua conta do Discord…'))));
    try {
      nickname = discordNickname(await rpc.user());
    } catch (err) {
      // Sem login do Discord (recusado ou servidor sem DISCORD_CLIENT_SECRET): nome manual.
      console.warn('[discord] login:', err);
    }
  }
  if (!nickname) return renderLogin();
  try {
    await handler.connect(serverUrl(), { nickname, uid: settings.get('uid') });
    renderLobby();
  } catch (err) {
    renderLogin(err?.message || 'Não foi possível conectar ao servidor.');
  }
}

start();
