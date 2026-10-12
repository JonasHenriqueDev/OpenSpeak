// Mini-helper de DOM: h('div.classe#id', {atributos, on:{evento}}, ...filhos)

export function h(selector, props, ...children) {
  if (props == null || typeof props !== 'object' || props instanceof Node || Array.isArray(props)) {
    children.unshift(props);
    props = {};
  }
  const [, tag = 'div', rest = ''] = selector.match(/^([a-z0-9-]+)?(.*)$/i);
  const el = document.createElement(tag || 'div');
  for (const part of rest.match(/[.#][^.#]+/g) ?? []) {
    if (part[0] === '.') el.classList.add(part.slice(1));
    else el.id = part.slice(1);
  }
  for (const [key, value] of Object.entries(props)) {
    if (value == null || value === false) continue;
    if (key === 'on') {
      for (const [ev, fn] of Object.entries(value)) el.addEventListener(ev, fn);
    } else if (key === 'class') {
      for (const [cls, on] of Object.entries(typeof value === 'string' ? { [value]: true } : value)) {
        if (on) cls.split(' ').filter(Boolean).forEach(c => el.classList.add(c));
      }
    } else if (key === 'style' && typeof value === 'object') {
      Object.assign(el.style, value);
    } else if (key === 'dataset') {
      Object.assign(el.dataset, value);
    } else if (key in el && typeof value !== 'string') {
      el[key] = value;
    } else if (key === 'html') {
      el.innerHTML = value;
    } else {
      el.setAttribute(key, value === true ? '' : value);
    }
  }
  append(el, children);
  return el;
}

function append(el, children) {
  for (const child of children.flat(Infinity)) {
    if (child == null || child === false) continue;
    el.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
}

export function clear(el) {
  while (el.firstChild) el.firstChild.remove();
  return el;
}

export function replaceChildren(el, ...children) {
  clear(el);
  append(el, children);
  return el;
}
