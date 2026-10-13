// Árvore de canais com clientes e indicadores de stream (ao vivo / espectadores).

import { h, replaceChildren } from './dom.js';
import { icon } from './icons.js';
import { t } from './i18n.js';
import { StreamType } from '../core/protocol.js';

const TYPE_ICON = { [StreamType.CAMERA]: 'camera', [StreamType.SCREEN]: 'screen', [StreamType.WINDOW]: 'window' };

export class ChannelTree {
  constructor({ handler, manager, contacts, onError, onStreamClick }) {
    this.handler = handler;
    this.manager = manager;
    this.contacts = contacts;
    this.onError = onError;
    this.onStreamClick = onStreamClick;
    this.el = h('nav.channel-tree');
    this.menu = null;
    document.addEventListener('click', () => this.#closeMenu());
  }

  render() {
    const hd = this.handler;
    const channels = [...hd.channels.values()].sort((a, b) => a.order - b.order);
    const ownCid = hd.ownClient?.cid;
    replaceChildren(this.el,
      h('div.tree-server', icon('server'), h('span.ellipsis', hd.serverName),
        hd.hasStreamingP2P ? h('span.tag', { title: 'capabilityExtensions' }, 'P2P') : null),
      channels.map(ch => {
        const clients = [...hd.clients.values()].filter(c => c.cid === ch.cid).sort((a, b) => a.nickname.localeCompare(b.nickname));
        const streams = hd.streamsInChannel(ch.cid);
        return h('div.tree-channel', { class: { current: ch.cid === ownCid } },
          h('div.tree-row.channel-row', {
            title: `${ch.description}\n(${t('app.channel.join')}: duplo clique)`,
            on: { dblclick: () => hd.moveTo(ch.cid).catch(this.onError) },
          }, icon('channel'), h('span.ellipsis', ch.name),
            streams.length ? h('span.channel-live', icon('stream'), String(streams.length)) : null),
          clients.map(c => this.#clientRow(c, streams.filter(s => s.ownerClid === c.clid), ch.cid === ownCid)));
      }));
  }

  #clientRow(client, streams, inMyChannel) {
    const isMe = client.clid === this.handler.ownClid;
    const isContact = this.contacts.has(client.uid);
    return h('div.tree-client',
      h('div.tree-row.client-row', {
        class: { me: isMe },
        on: { contextmenu: ev => { if (!isMe) { ev.preventDefault(); this.#openMenu(ev, client); } } },
      },
        h('span.talk-dot'),
        h('span.ellipsis', client.nickname),
        isMe ? h('span.muted', `(${t('app.you')})`) : null,
        isContact ? h('span.contact-star', { title: t('app.contacts') }, icon('star')) : null,
        h('span.client-ping', { title: 'Ping desta pessoa até o servidor (RTT)' },
          Number.isFinite(client.pingMs) && client.pingMs >= 0 ? `${Math.round(client.pingMs)} ms` : '—')),
      streams.map(s => {
        const watching = this.manager.inbound.has(s.streamIdentifier);
        return h('div.tree-row.stream-row', {
          class: { watching, clickable: inMyChannel && !isMe },
          title: s.name,
          on: { click: () => inMyChannel && this.onStreamClick(s.streamIdentifier) },
        }, icon(TYPE_ICON[s.type] ?? 'stream'), h('span.ellipsis', s.name),
          h('span.live-badge', t('video.stream.live')),
          h('span.viewer-count', icon('users'), String(s.viewerCount)));
      }));
  }

  #openMenu(ev, client) {
    this.#closeMenu();
    const isContact = this.contacts.has(client.uid);
    this.menu = h('div.context-menu', { style: { left: `${ev.clientX}px`, top: `${ev.clientY}px` } },
      h('button', { on: { click: () => { this.contacts.toggle(client.uid); this.render(); } } },
        icon('star'), t(isContact ? 'app.contacts.remove' : 'app.contacts.add')));
    document.body.append(this.menu);
  }

  #closeMenu() {
    this.menu?.remove();
    this.menu = null;
  }
}
