// Ícone + contagem de espectadores. Nas minhas transmissões, passar o mouse
// mostra quem está assistindo (lista local das sessões, não pública).

import { h } from './dom.js';
import { icon } from './icons.js';
import { t } from './i18n.js';

/** @param {() => string[] | null} names  apelidos dos espectadores, ou null se o stream não é meu */
export function viewerCount(count, names) {
  return h('span.viewer-count', {
    title: t('video.viewers'),
    on: {
      mouseenter: ev => {
        const list = names();
        if (!list) return;
        ev.currentTarget.title = list.length
          ? `${t('video.viewers.active')} (${list.length})\n${list.join('\n')}`
          : t('video.viewers.active.empty');
      },
    },
  }, icon('users'), String(count ?? 0));
}
