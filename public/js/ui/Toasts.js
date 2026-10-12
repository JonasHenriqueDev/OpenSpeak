// Notificações flutuantes (pedidos de entrada, erros).

import { h } from './dom.js';
import { icon } from './icons.js';

export class Toasts {
  constructor() {
    this.el = h('div.toasts', { 'aria-live': 'polite' });
    this.byKey = new Map();
  }

  /**
   * @param {{key?:string, kind?:'info'|'error'|'request', icon?:string, text:string,
   *          actions?:{label:string, cls?:string, title?:string, onClick:()=>void}[], timeout?:number}} o
   */
  show({ key, kind = 'info', icon: iconName, text, actions = [], timeout = 6000 }) {
    if (key) this.dismiss(key);
    const toast = h('div.toast', { class: kind },
      icon(iconName ?? (kind === 'error' ? 'warning' : 'info')),
      h('div.toast-text', text),
      actions.length ? h('div.toast-actions', actions.map(a => h('button.btn.small', {
        class: a.cls ?? '', title: a.title ?? '',
        on: { click: () => { close(); a.onClick(); } },
      }, a.label))) : null,
      h('button.icon-btn.toast-close', { on: { click: () => close() } }, icon('close')));
    const close = () => {
      toast.remove();
      if (key && this.byKey.get(key) === toast) this.byKey.delete(key);
    };
    if (key) this.byKey.set(key, toast);
    this.el.append(toast);
    if (timeout) setTimeout(close, timeout);
    return close;
  }

  dismiss(key) {
    this.byKey.get(key)?.remove();
    this.byKey.delete(key);
  }

  error(err) {
    const text = err?.message || String(err);
    console.error('[openspeak]', err);
    this.show({ kind: 'error', text });
  }
}
