// Handshake mínimo com o Discord (protocolo do Embedded App SDK, sem a biblioteca).
// O Discord abre a atividade com ?frame_id=…&instance_id=…&platform=… em
// https://<client_id>.discordsays.com e espera o HANDSHAKE por postMessage.

const Opcode = Object.freeze({ HANDSHAKE: 0, FRAME: 1, CLOSE: 2, HELLO: 3 });

export function discordContext(loc = location) {
  const params = new URLSearchParams(loc.search);
  const frameId = params.get('frame_id');
  const clientId = loc.hostname.match(/^(\d+)\.discordsays\.com$/)?.[1] ?? null;
  if (!frameId || !params.get('instance_id')) return null;
  return { frameId, clientId, platform: params.get('platform'), channelId: params.get('channel_id') };
}

/** Avisa o Discord que a atividade carregou (tira a tela de carregamento). */
export function discordHandshake(ctx) {
  if (!ctx?.clientId) return;
  const target = window.opener ?? window.parent;
  if (!target || target === window) return;
  let origin = '*';
  try { if (document.referrer) origin = new URL(document.referrer).origin; } catch {}
  target.postMessage([Opcode.HANDSHAKE, { v: 1, encoding: 'json', client_id: ctx.clientId, frame_id: ctx.frameId }], origin);
}
