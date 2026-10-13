// Gerenciar espectadores: pedidos, assistindo agora, bloqueados.

import { h, replaceChildren } from './dom.js';
import { icon } from './icons.js';
import { t } from './i18n.js';
import { SessionState } from '../video/StreamBase.js';
import { StreamMode, RELAY_CLID } from '../core/protocol.js';

const STATE_CLASS = {
  [SessionState.CONNECTED]: 'ok',
  [SessionState.CONNECTING]: 'pending',
  [SessionState.RECONNECTING]: 'pending',
  [SessionState.FAILED]: 'bad',
};

export class ViewersPanel {
  constructor({ handler, manager, streamIdentifier, onError }) {
    this.handler = handler;
    this.manager = manager;
    this.streamIdentifier = streamIdentifier;
    this.onError = onError;
    this.title = t('video.action.manage_viewers');
    this.el = h('div.panel-body.viewers');
  }

  render() {
    const own = this.manager.ownStreams.get(this.streamIdentifier);
    if (!own) {
      replaceChildren(this.el, h('p.muted', t('video.stream.ended')));
      return;
    }
    const m = this.manager;
    const id = this.streamIdentifier;
    const guard = p => p.catch(err => this.onError(err));
    const limitReached = own.config.viewerLimit > 0 && own.viewers.size >= own.config.viewerLimit;

    const requests = [...own.joinRequests.values()];
    const viewers = [...own.viewers];
    const banned = [...own.banned];

    replaceChildren(this.el,
      h('section.viewer-section',
        h('h4', t('video.viewers.join_request'), h('span.count', String(requests.length))),
        limitReached && requests.length ? h('div.hint.warn', t('video.join.request.viewer_limit_reached')) : null,
        requests.length ? requests.map(r => h('div.viewer-row',
          icon('user'), h('span.ellipsis', r.nickname),
          h('span.spacer'),
          h('button.icon-btn.ok', {
            title: t('video.join.request.accept'), disabled: limitReached,
            on: { click: () => guard(m.respondJoinRequest(id, r.clid, { accept: true })) },
          }, icon('check')),
          h('button.icon-btn', {
            title: t('video.join.request.deny'),
            on: { click: () => guard(m.respondJoinRequest(id, r.clid, { accept: false })) },
          }, icon('close')),
          h('button.icon-btn.danger', {
            title: t('video.join.request.block.tooltip'),
            on: { click: () => guard(m.respondJoinRequest(id, r.clid, { accept: false, ban: true })) },
          }, icon('ban')),
        )) : h('p.muted', t('video.viewers.join_request.empty'))),

      h('section.viewer-section',
        h('h4', t('video.viewers.active'), h('span.count',
          own.config.viewerLimit ? `${viewers.length}/${own.config.viewerLimit}` : String(viewers.length))),
        viewers.length ? viewers.map(clid => {
          // SFU: não há sessão por espectador; mostra a conexão com o servidor.
          const session = own.sessions.get(own.config.mode === StreamMode.SFU ? RELAY_CLID : clid)?.session;
          const codec = session?.activeCodec?.replace('video/', '') ?? '';
          return h('div.viewer-row',
            h('span.state-dot', { class: STATE_CLASS[session?.state] ?? 'pending', title: session?.state ?? '' }),
            h('span.ellipsis', this.handler.clientName(clid)),
            codec ? h('span.tag', codec) : null,
            h('span.spacer'),
            h('button.icon-btn', {
              title: t('video.viewers.action.kick'),
              on: { click: () => guard(m.removeViewer(id, clid)) },
            }, icon('kick')),
            h('button.icon-btn.danger', {
              title: t('video.viewers.action.ban'),
              on: { click: () => guard(m.removeViewer(id, clid, { ban: true })) },
            }, icon('ban')));
        }) : h('p.muted', t('video.viewers.active.empty'))),

      h('section.viewer-section',
        h('h4', t('video.viewers.banned'), h('span.count', String(banned.length))),
        banned.length ? banned.map(uid => {
          const known = [...this.handler.clients.values()].find(c => c.uid === uid);
          return h('div.viewer-row',
            icon('ban'), h('span.ellipsis', known?.nickname ?? uid.slice(0, 10)),
            h('span.spacer'),
            h('button.btn.ghost.small', { on: { click: () => guard(m.unban(id, uid)) } }, t('video.viewers.action.unban')));
        }) : h('p.muted', t('video.viewers.banned.empty'))),
    );
  }
}
