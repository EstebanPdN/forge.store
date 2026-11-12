// One navigation family for categories, detail tabs and the GitHub reader.
// Integer destinations keep labels sharp once the shared motion has settled.
import { h, clamp } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { glyph } from './components.js';

export const TAB_MOTION = { duration: 340, easing: 'cubic-bezier(0.22, 1, 0.36, 1)' };

export function revealTabContent(element, direction = 1) {
  if (!element) return;
  element._tabAnimation?.cancel();
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  element._tabAnimation = element.animate(
    [{ opacity: 0, transform: `translateX(${Math.sign(direction) * 12}px)` }, { opacity: 1, transform: 'none' }],
    { ...TAB_MOTION, duration: reduced ? 0 : TAB_MOTION.duration, fill: 'none' },
  );
}

export class TabBar {
  constructor(options, value, onChange, { label = 'Pestañas', wrap = false } = {}) {
    this.value = value;
    this.onChange = onChange;
    this.wrap = wrap;
    this.thumb = h('i.nav-thumb', { 'aria-hidden': 'true' });
    this.track = h('div.nav-track');
    this.viewport = h('div.nav-viewport', this.thumb, this.track);
    this.prev = h('button.nav-step', { type: 'button', 'aria-label': 'L · Anterior', on: { click: () => this.step(-1) } }, glyph('L'));
    this.next = h('button.nav-step', { type: 'button', 'aria-label': 'R · Siguiente', on: { click: () => this.step(1) } }, glyph('R'));
    this.el = h('div.nav-tabs', { role: 'group', 'aria-label': label }, this.prev, this.viewport, this.next);
    // Preserve the existing scene API while sharing the actual component.
    this.el.seg = { set: (v) => this.set(v), sync: (animate) => this.sync(animate), get: () => this.value, step: (dir) => this.step(dir) };
    this.el.addEventListener('keydown', (e) => {
      if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) return;
      e.preventDefault();
      e.stopPropagation();
      if (e.key === 'Home' || e.key === 'End') this.choose(this.options[e.key === 'Home' ? 0 : this.options.length - 1].value);
      else this.step(e.key === 'ArrowRight' ? 1 : -1);
      this.buttons.find((b) => b.dataset.value === String(this.value))?.focus({ preventScroll: true });
    });
    this.setOptions(options);
    this.resize = new ResizeObserver(() => this.sync(false));
    this.resize.observe(this.viewport);
    document.fonts.ready.then(() => this.sync(false));
  }
  setOptions(options) {
    this.options = options;
    this.buttons = options.map(({ value, label, icon: ic }) => h('button.nav-tab', {
      type: 'button', dataset: { value }, on: { click: () => this.choose(value) },
    }, ic ? icon(ic) : null, h('span', label)));
    this.track.replaceChildren(...this.buttons);
    this.set(this.value, false);
    requestAnimationFrame(() => this.sync(false));
  }
  choose(value) {
    if (value === this.value) return;
    this.set(value);
    this.onChange?.(value);
  }
  set(value, animated = true) {
    if (!this.options.some((o) => o.value === value)) return;
    this.value = value;
    this.buttons.forEach((b) => {
      const selected = b.dataset.value === String(value);
      b.setAttribute('aria-pressed', String(selected));
      b.tabIndex = selected ? 0 : -1;
    });
    this.sync(animated);
  }
  step(direction) {
    const n = this.options.length;
    if (!n) return;
    let i = this.options.findIndex((o) => o.value === this.value) + direction;
    i = this.wrap ? (i + n) % n : clamp(i, 0, n - 1);
    this.choose(this.options[i].value);
  }
  sync(animated = true) {
    const active = this.buttons.find((b) => b.dataset.value === String(this.value));
    if (!active || !this.viewport.clientWidth) return;
    const width = this.viewport.clientWidth;
    // Give both the label and its highlight the same integer geometry. Natural
    // text widths are fractional; rounding only the highlight drifts off-centre.
    this.buttons.forEach((b) => { b.style.width = ''; });
    const widths = this.buttons.map((b) => Math.ceil(parseFloat(getComputedStyle(b).width)));
    const inset = parseFloat(getComputedStyle(this.el).getPropertyValue('--tab-inset'));
    const spare = Math.max(0, width - 2 * inset - widths.reduce((sum, w) => sum + w, 0));
    this.buttons.forEach((b, i) => {
      b.style.width = `${widths[i] + Math.floor(spare / widths.length) + (i < spare % widths.length ? 1 : 0)}px`;
    });
    const max = Math.max(0, this.track.scrollWidth - width);
    const shift = Math.round(clamp(active.offsetLeft + active.offsetWidth / 2 - width / 2, 0, max));
    for (const el of [this.track, this.thumb]) el.style.transitionDuration = animated ? '' : '0s';
    this.track.style.transform = `translateX(${-shift}px)`;
    this.thumb.style.width = `${active.offsetWidth}px`;
    this.thumb.style.transform = `translateX(${active.offsetLeft - shift}px)`;
    this.el.dataset.left = String(shift > 0);
    this.el.dataset.right = String(shift < max);
    const i = this.options.findIndex((o) => o.value === this.value);
    this.prev.disabled = !this.wrap && i === 0;
    this.next.disabled = !this.wrap && i === this.options.length - 1;
  }
  destroy() { this.resize.disconnect(); }
}
