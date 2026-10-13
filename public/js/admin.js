// Painel admin. Todo texto vindo da sala (nicks, motivos) entra via textContent.

const $ = id => document.getElementById(id);
const REFRESH_MS = 5000;
let timer = null, current = null, banTarget = null;

function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (k === 'class') node.className = v;
    else if (k === 'onclick') node.addEventListener('click', v);
    else node[k] = v;
  }
  for (const c of children) if (c != null) node.append(c);
  return node;
}

async function api(action, body) {
  const res = await fetch(`/api/admin?action=${action}`, body === undefined
    ? { credentials: 'same-origin' }
    : { method: 'POST', credentials: 'same-origin',
        headers: { 'content-type': 'application/json', 'x-openspeak-admin': '1' }, body: JSON.stringify(body) });
  let data = {};
  try { data = await res.json(); } catch { /* non-JSON (e.g. local server without the API) */ }
  if (res.status === 401 && action !== 'login') { showLogin(); throw new Error(data.error || 'Sessão expirada.'); }
  if (!res.ok) throw new Error(data.error || `Erro ${res.status}`);
  return data;
}

let toastTimer;
function toast(message, error = false) {
  const t = $('toast');
  t.textContent = message;
  t.className = `toast${error ? ' error' : ''}`;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 3500);
}

const ago = ms => {
  const s = Math.max(0, Math.round(ms / 1000));
  if (s < 60) return `${s}s`;
  if (s < 3600) return `${Math.floor(s / 60)} min`;
  return `${Math.floor(s / 3600)} h ${Math.floor((s % 3600) / 60)} min`;
};
const until = ts => ts === null ? 'permanente'
  : `até ${new Date(ts).toLocaleString('pt-BR', { dateStyle: 'short', timeStyle: 'short' })}`;

function showLogin(message = '') {
  clearInterval(timer); timer = null;
  $('panel').hidden = true;
  $('login').hidden = false;
  $('login-error').textContent = message;
  $('password').focus();
}

function showPanel() {
  $('login').hidden = true;
  $('panel').hidden = false;
  refresh();
  clearInterval(timer);
  timer = setInterval(refresh, REFRESH_MS);
}

async function refresh() {
  try {
    render(await api('status'));
  } catch (err) {
    if (!$('panel').hidden) { $('updated').textContent = 'Sem conexão'; $('updated').className = 'updated bad'; }
  }
}

function render(data) {
  current = data;
  const { info } = data;
  $('build').textContent = `v${info.version} · ${info.env}${info.commit ? ` · ${info.commit}` : ''}${info.region ? ` · ${info.region}` : ''}`;
  $('updated').textContent = `Atualizado ${new Date().toLocaleTimeString('pt-BR')}`;
  $('updated').className = 'updated dim';

  const banner = $('banner');
  if (!info.redis) { banner.textContent = 'REDIS_URL não está configurada: a sala web não funciona até ela ser definida.'; banner.hidden = false; }
  else if (!data.redis) { banner.textContent = 'O Redis não respondeu. Participantes podem estar sem sinalização.'; banner.hidden = false; }
  else banner.hidden = true;

  $('s-redis').textContent = data.redis ? 'Online' : 'Offline';
  $('s-redis').className = `value ${data.redis ? 'ok' : 'bad'}`;
  const clients = data.clients ?? [];
  $('s-clients').textContent = data.redis ? `${clients.length} / ${data.maxClients}` : '—';
  $('s-streams').textContent = data.redis ? String(data.streams) : '—';
  const lock = $('lock-toggle');
  lock.disabled = !data.redis;
  $('s-lock').textContent = !data.redis ? '—' : data.locked ? 'Trancada' : 'Aberta';
  $('s-lock').className = `value ${data.locked ? 'warn' : 'ok'}`;
  lock.textContent = data.locked ? 'Destrancar' : 'Trancar';
  $('reset').disabled = !data.redis;

  const now = Date.now();
  $('clients').replaceChildren(...clients.map(c => {
    const away = c.lastSeen && now - c.lastSeen > 45_000;
    const state = el('td', {},
      ...c.streams.map(s => el('span', { class: 'tag live', title: s.name, textContent: `● ao vivo · ${s.viewers} assistindo` })),
      c.inputMuted ? el('span', { class: 'tag', textContent: 'mic off' }) : null,
      c.outputMuted ? el('span', { class: 'tag', textContent: 'som off' }) : null,
      away ? el('span', { class: 'tag', textContent: 'reconectando' }) : null);
    return el('tr', {},
      el('td', {}, el('span', { class: 'nick', textContent: c.nickname }), el('span', { class: 'sub mono', textContent: `#${c.clid}` })),
      el('td', { textContent: c.channel }),
      state,
      el('td', { textContent: c.since ? ago(now - c.since) : '—' }),
      el('td', { class: 'mono', textContent: c.ipHash ? c.ipHash.slice(0, 8) : '—' }),
      el('td', { class: 'actions' },
        el('button', { class: 'btn small', textContent: 'Expulsar', onclick: () => kick(c) }),
        el('button', { class: 'btn small danger', textContent: 'Banir', onclick: () => openBan(c) })));
  }));
  $('clients-empty').hidden = clients.length > 0 || !data.redis;

  const bans = data.bans ?? [];
  $('bans').replaceChildren(...bans.map(b => el('li', {},
    el('div', { class: 'info' },
      el('span', { class: 'nick', textContent: b.nickname || b.uid }),
      el('span', { class: 'sub', textContent: [until(b.until), b.reason].filter(Boolean).join(' · ') })),
    el('button', { class: 'btn small', textContent: 'Remover', onclick: () => unban(b) }))));
  $('bans-empty').hidden = bans.length > 0 || !data.redis;
}

async function action(name, body, success) {
  try {
    const result = await api(name, body);
    toast(success(result));
    await refresh();
  } catch (err) { toast(err.message, true); }
}

function kick(c) {
  if (!confirm(`Expulsar ${c.nickname}? A pessoa poderá voltar depois de 1 minuto.`)) return;
  action('kick', { clid: c.clid }, () => `${c.nickname} foi expulso(a).`);
}

function openBan(c) {
  banTarget = c;
  $('ban-name').textContent = c.nickname;
  $('ban-reason').value = '';
  $('ban-dialog').showModal();
}

$('ban-dialog').addEventListener('close', () => {
  const c = banTarget; banTarget = null;
  if ($('ban-dialog').returnValue !== 'confirm' || !c) return;
  const value = $('ban-minutes').value;
  action('ban', { clid: c.clid, minutes: value === 'permanent' ? null : Number(value), reason: $('ban-reason').value },
    () => `${c.nickname} foi banido(a).`);
});

function unban(b) {
  action('unban', { id: b.id }, () => `Banimento de ${b.nickname || b.uid} removido.`);
}

$('lock-toggle').addEventListener('click', () => {
  const locked = !current?.locked;
  if (locked && !confirm('Trancar a sala? Quem já está dentro continua; ninguém novo consegue entrar.')) return;
  action('lock', { locked }, r => (r.locked ? 'Sala trancada.' : 'Sala aberta.'));
});

$('reset').addEventListener('click', () => {
  if (!confirm('Reiniciar a sala? Todos serão desconectados e as transmissões encerradas.')) return;
  action('reset', {}, r => `Sala reiniciada (${r.disconnected} desconectado(s)).`);
});

$('logout').addEventListener('click', async () => {
  try { await api('logout', {}); } catch { /* cookie is cleared server-side anyway */ }
  showLogin();
});

$('login-form').addEventListener('submit', async ev => {
  ev.preventDefault();
  const button = ev.submitter;
  button.disabled = true;
  $('login-error').textContent = '';
  try {
    await api('login', { password: $('password').value });
    $('password').value = '';
    showPanel();
  } catch (err) {
    $('login-error').textContent = err.message;
  } finally { button.disabled = false; }
});

// Probe the session cookie; 401 lands on the login form.
api('status').then(() => showPanel()).catch(err => {
  if ($('login').hidden) showLogin(err.message === 'Sessão expirada.' ? '' : err.message);
});
