// Small DOM helpers: element factory, animation promises and timing.

/**
 * h('div.card.big#id', { on: { click }, style: {...}, dataset: {...} }, ...children)
 */
export function h(spec, props, ...children) {
  if (props instanceof Node || typeof props === 'string' || Array.isArray(props)) {
    children.unshift(props);
    props = null;
  }
  const [, tag = 'div', rest = ''] = spec.match(/^([a-z0-9-]*)(.*)$/i);
  const el = document.createElement(tag || 'div');
  for (const part of rest.match(/[.#][^.#]+/g) || []) {
    if (part[0] === '.') el.classList.add(part.slice(1));
    else el.id = part.slice(1);
  }
  if (props) {
    for (const [key, value] of Object.entries(props)) {
      if (value == null || value === false) continue;
      if (key === 'on') for (const [ev, fn] of Object.entries(value)) el.addEventListener(ev, fn);
      else if (key === 'style' && typeof value === 'object') {
        for (const [prop, v] of Object.entries(value)) {
          if (v == null) continue;
          if (prop.startsWith('--')) el.style.setProperty(prop, v);
          else el.style[prop] = v;
        }
      } else if (key === 'dataset') Object.assign(el.dataset, value);
      else if (key === 'class') el.className += ' ' + value;
      else if (key === 'html') el.innerHTML = value;
      else if (key === 'text') el.textContent = value;
      else if (value === true) el.setAttribute(key, '');
      else el.setAttribute(key, value);
    }
  }
  append(el, children);
  return el;
}

export function append(el, children) {
  for (const child of children.flat(Infinity)) {
    if (child == null || child === false) continue;
    el.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return el;
}

/** Parses an SVG or HTML string into a single element. */
export function fromHTML(markup) {
  const t = document.createElement('template');
  t.innerHTML = markup.trim();
  return t.content.firstElementChild;
}

export const $ = (sel, root = document) => root.querySelector(sel);
export const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

export const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
export const nextFrame = () => new Promise((resolve) => requestAnimationFrame(() => resolve()));
export const clamp = (v, min, max) => Math.min(max, Math.max(min, v));
export const lerp = (a, b, t) => a + (b - a) * t;

/** Web Animations API wrapper that resolves when the animation ends or is cancelled. */
export function animate(el, keyframes, options) {
  if (!el) return Promise.resolve();
  const opts = typeof options === 'number' ? { duration: options } : { ...options };
  opts.easing ??= 'cubic-bezier(0.22, 1, 0.36, 1)';
  opts.fill ??= 'both';
  const animation = el.animate(keyframes, opts);
  return animation.finished.then(
    () => animation,
    () => animation,
  );
}

/** Commits the final animated values and removes the animation (keeps styles clean). */
export async function settle(el, keyframes, options) {
  const animation = await animate(el, keyframes, options);
  try {
    animation.commitStyles();
  } catch {}
  animation.cancel();
}

export function stagger(elements, keyframes, { step = 40, delay = 0, ...options } = {}) {
  return Promise.all(
    [...elements].map((el, i) => animate(el, keyframes, { ...options, delay: delay + i * step })),
  );
}

/** Restartable CSS class animation */
export function pulseClass(el, cls, ms = 400) {
  el.classList.remove(cls);
  void el.offsetWidth;
  el.classList.add(cls);
  clearTimeout(el[`_${cls}`]);
  el[`_${cls}`] = setTimeout(() => el.classList.remove(cls), ms);
}
