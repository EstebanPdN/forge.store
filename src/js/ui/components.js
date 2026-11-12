// Reusable screen components: app icons, status bar, toasts, segmented controls,
// touch/D-pad scrollers and button glyphs.
import { h, animate, clamp, fromHTML } from '../core/dom.js';
import { icon, glyphIcon } from '../core/icons.js';
import { t, fmtSize } from '../core/i18n.js';
import { SD_CAPACITY } from '../core/sd.js';

const ARROWS = { '◀': 'M7.2 3 3.4 5.5 7.2 8Z', '▶': 'M3.8 3 7.6 5.5 3.8 8Z', '▲': 'M3 7.1 5.5 3.3 8 7.1Z', '▼': 'M3 3.9 5.5 7.7 8 3.9Z' };

/**
 * Button badge. Single letters are drawn in SVG and centred on the capital's own
 * height (Nunito caps are 0.705 em), so A / B / X / Y sit dead centre in every
 * browser instead of following the font's line box.
 */
export function glyph(button) {
  // + and − are plain CSS bars: inline SVG strokes drift by a fraction of a pixel
  // under the device zoom, which shows on such a small, symmetric mark.
  if (button === 'START' || button === 'SELECT') return h('span.glyph.system', { dataset: { g: button }, 'aria-label': button }, button);
  const mark = ARROWS[button]
    ? `<path d="${ARROWS[button]}" fill="currentColor"/>`
    : `<text x="5.5" y="${(5.5 + (0.705 * 7) / 2).toFixed(2)}" text-anchor="middle" font-size="7" font-weight="900">${button}</text>`;
  return h('span.glyph.svg', { dataset: { g: button }, html: `<svg viewBox="0 0 11 11" aria-hidden="true">${mark}</svg>` });
}

export function appIcon(app, { size = 48, cls = '' } = {}) {
  const el = h('div.app-icon' + (cls ? '.' + cls : ''), {
    style: { width: `${size}px`, height: `${size}px`, borderRadius: `${Math.round(size * 0.21)}px` },
  });
  if (app.status === 'development') el.classList.add('dev');
  if (app.pixelIcon) el.classList.add('pixel');
  if (typeof app.icon === 'string') {
    const img = h('img', { src: app.icon, alt: '', draggable: 'false', loading: 'lazy' });
    // Remote icons (Universal-DB) can be missing or blocked: fall back to a generated tile.
    img.addEventListener('error', () => img.replaceWith(glyphIcon({ glyph: 'box', bg: [app.color || '#556070', '#2a2f37'], fg: '#ffffff' }, size)), { once: true });
    el.append(img);
  }
  else el.append(glyphIcon(app.icon || {}, size));
  return el;
}

export class StatusBar {
  constructor(screen) {
    this.section = h('span.sb-section');
    this.dl = h('span.sb-dl', icon('download'));
    this.clock = h('span.sb-clock');
    // Symmetric Wi-Fi silhouette, centred in its own box (not on a text baseline).
    this.wifi = fromHTML(
      `<svg class="sb-wifi" viewBox="0 0 16 14" fill="none" aria-hidden="true"><path d="M1.8 4.5a9 9 0 0 1 12.4 0" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/><path d="M4.5 7.2a5 5 0 0 1 7 0" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/><circle cx="8" cy="10.5" r="1.25" fill="currentColor"/></svg>`,
    );
    // Battery in plain HTML boxes (crisp at any zoom), with its percentage beside it.
    this.batteryFill = h('i.sb-fill');
    this.batteryPct = h('span.sb-pct');
    this.battery = h('span.sb-battery', h('span.sb-cell', this.batteryFill), h('span.sb-nub'));
    this.el = h(
      'div.statusbar.hidden',
      h('div.sb-pill', h('img.pixel', { src: 'assets/brand/forge-icon.png', alt: '' }), h('span.sb-brand', h('b', 'forge.'), 'store'), this.section),
      h('div.sb-pill.right', this.dl, this.wifi, this.clock, h('span.sb-power', this.battery, this.batteryPct)),
    );
    screen.append(this.el);
    this.tick();
    setInterval(() => this.tick(), 10_000);
  }
  tick() {
    const now = new Date();
    this.clock.textContent = now.toLocaleTimeString('es', { hour: '2-digit', minute: '2-digit', hour12: false });
    // A slow, believable battery drain for the prototype.
    const level = 0.92 - ((now.getMinutes() % 60) / 60) * 0.3;
    const pct = Math.round(level * 100);
    this.batteryFill.style.width = `${Math.round(pct / 100 * 16)}px`;
    this.batteryPct.textContent = `${pct}%`;
    this.battery.classList.toggle('low', pct <= 20);
  }
  setSection(text) {
    this.section.textContent = text;
    this.section.hidden = !text;
  }
  setDownloading(on) {
    this.dl.classList.toggle('on', on);
  }
  show(visible) {
    this.el.classList.toggle('hidden', !visible);
  }
}

export class Toasts {
  constructor(screen) {
    this.el = h('div.toasts');
    screen.append(this.el);
  }
  show({ text, icon: ic, image, tone = 'mint', ms = 2600 }) {
    const badge = h('span.t-icon', { style: { background: tone === 'forge' ? 'var(--forge)' : tone === 'amber' ? 'var(--amber)' : tone === 'danger' ? 'var(--danger)' : 'var(--mint)' } });
    if (image) badge.append(image);
    else badge.append(icon(ic || 'check'));
    const toast = h('div.toast', badge, h('span', text));
    this.el.append(toast);
    animate(toast, [{ opacity: 0, transform: 'translateY(-10px) scale(.94)' }, { opacity: 1, transform: 'none' }], 360);
    setTimeout(async () => {
      await animate(toast, [{ opacity: 1 }, { opacity: 0, transform: 'translateY(-6px)' }], { duration: 260, easing: 'ease-in' });
      toast.remove();
    }, ms);
  }
}

/** Segmented control: options = [{ value, label }]. */
export function segmented(options, value, onChange, { small = false } = {}) {
  const thumb = h('i.thumb');
  const buttons = options.map((opt) =>
    h('button', { type: 'button', 'aria-pressed': String(opt.value === value), dataset: { value: opt.value }, on: { click: () => set(opt.value, true) } }, opt.label),
  );
  const el = h('div.ui-seg' + (small ? '.small' : ''), thumb, buttons);
  let current = value;
  function sync(animated = true) {
    const btn = buttons.find((b) => b.dataset.value === String(current));
    if (!btn) return;
    thumb.style.transition = animated ? '' : 'none';
    thumb.style.width = `${btn.offsetWidth}px`;
    thumb.style.transform = `translateX(${btn.offsetLeft}px)`;
  }
  function set(v, user) {
    if (v === current && user) return;
    current = v;
    buttons.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.value === String(v))));
    sync();
    if (user) onChange?.(v);
  }
  requestAnimationFrame(() => sync(false));
  el.seg = { set, sync, get: () => current, step: (dir) => {
    const i = options.findIndex((o) => o.value === current);
    const next = options[clamp(i + dir, 0, options.length - 1)];
    if (next.value !== current) set(next.value, true);
    return next.value;
  } };
  return el;
}

/** Vertical scroller with touch drag + inertia, wheel and programmatic steps. */
export class Scroller {
  constructor(el, content) {
    this.el = el;
    this.content = content;
    this.y = 0;
    this.v = 0;
    this.bar = h('div.scrollbar');
    el.append(this.bar);
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.scrollTo(this.y + e.deltaY * 0.4, false);
      this.flash();
    }, { passive: false });
    el.addEventListener('pointerdown', (e) => this.drag(e));
  }
  get max() {
    return Math.max(0, this.content.scrollHeight - this.el.clientHeight);
  }
  drag(e) {
    const startY = e.clientY;
    const startScroll = this.y;
    const zoom = this.el.getBoundingClientRect().height / this.el.clientHeight || 1;
    let lastY = e.clientY;
    let lastT = performance.now();
    let moved = false;
    cancelAnimationFrame(this.raf);
    const move = (ev) => {
      const dy = (ev.clientY - startY) / zoom;
      if (Math.abs(dy) > 3) moved = true;
      if (!moved) return;
      const now = performance.now();
      this.v = ((lastY - ev.clientY) / zoom / Math.max(1, now - lastT)) * 16;
      lastY = ev.clientY;
      lastT = now;
      this.scrollTo(startScroll - dy, false, true);
      this.flash();
    };
    const up = () => {
      removeEventListener('pointermove', move);
      removeEventListener('pointerup', up);
      if (moved) {
        // Swallow the click that ends a drag.
        this.el.addEventListener('click', (ev) => ev.stopPropagation(), { capture: true, once: true });
        this.inertia();
      }
    };
    addEventListener('pointermove', move);
    addEventListener('pointerup', up);
  }
  inertia() {
    const step = () => {
      this.v *= 0.92;
      if (Math.abs(this.v) < 0.2) return;
      this.scrollTo(this.y + this.v, false);
      this.raf = requestAnimationFrame(step);
    };
    this.raf = requestAnimationFrame(step);
  }
  scrollTo(y, smooth = true, rubber = false) {
    const max = this.max;
    let target = rubber ? (y < 0 ? y * 0.35 : y > max ? max + (y - max) * 0.35 : y) : clamp(y, 0, max);
    target = Math.round(target);
    if (!rubber) target = clamp(target, 0, max);
    this.y = target;
    this.content.style.transition = smooth ? 'transform .35s cubic-bezier(0.22, 1, 0.36, 1)' : 'none';
    this.content.style.transform = `translateY(${-target}px)`;
    this.syncBar();
    if (rubber && (y < 0 || y > max)) {
      clearTimeout(this.snap);
      this.snap = setTimeout(() => this.scrollTo(clamp(this.y, 0, this.max)), 120);
    }
  }
  by(dy) {
    this.scrollTo(this.y + dy);
    this.flash();
  }
  reset() {
    this.scrollTo(0, false);
  }
  /** Scrolls just enough to show a child element. */
  reveal(child, margin = 8) {
    const top = child.offsetTop;
    const bottom = top + child.offsetHeight;
    const view = this.el.clientHeight;
    if (top - margin < this.y) this.scrollTo(top - margin);
    else if (bottom + margin > this.y + view) this.scrollTo(bottom + margin - view);
  }
  syncBar() {
    const view = this.el.clientHeight;
    const total = this.content.scrollHeight;
    if (total <= view + 1) {
      this.bar.style.display = 'none';
      return;
    }
    this.bar.style.display = '';
    const height = Math.max(14, (view / total) * view);
    const top = (this.y / Math.max(1, total - view)) * (view - height);
    this.bar.style.height = `${height}px`;
    this.bar.style.top = `${clamp(top, 0, view - height)}px`;
  }
  flash() {
    this.el.classList.add('scrolling');
    clearTimeout(this.flashTimer);
    this.flashTimer = setTimeout(() => this.el.classList.remove('scrolling'), 700);
  }
}

export function ripple(screen, clientX, clientY) {
  const rect = screen.getBoundingClientRect();
  const scale = rect.width / screen.offsetWidth;
  const el = h('i.ripple', { style: { left: `${(clientX - rect.left) / scale}px`, top: `${(clientY - rect.top) / scale}px` } });
  screen.append(el);
  setTimeout(() => el.remove(), 500);
}

/** A labelled list row used by settings and detail facts. */
export function factRow(label, value) {
  return h('div.fact', h('span.k', label), h('span.v', value));
}

export function statusChip(status) {
  switch (status.kind) {
    case 'installed':
      return h('span.chip.mint', icon('check'), t('status.installed'));
    case 'update':
      return h('span.chip.forge', icon('update'), t('status.update'));
    case 'downloading':
      return h('span.chip.forge', icon('download'), t('status.downloading'));
    case 'queued':
      return h('span.chip.forge', icon('clock'), t('status.queued'));
    case 'dev':
      return h('span.chip.lav', icon('hammer'), t('status.dev'));
    case 'needs-file':
      return h('span.chip.amber', icon('warn'), t('status.needsFile'));
    default:
      return null;
  }
}

/** SD capacity bar: store installs, the player's own files, system and free space. */
export function sdMeter(sd) {
  const used = sd.used();
  const store = sd.storeBytes();
  const user = sd.bytes((n) => n.kind === 'user');
  const other = Math.max(0, used - store - user);
  const pct = (v) => `${Math.max(v > 0 ? 0.6 : 0, (v / SD_CAPACITY) * 100)}%`;
  return h(
    'div.sd-meter',
    h('div.head', h('b', t('downloads.sd')), h('span', t('downloads.free', { free: fmtSize(SD_CAPACITY - used), total: fmtSize(SD_CAPACITY) }))),
    h('div.cap-bar', h('i.other', { style: { width: pct(other) } }), h('i.user', { style: { width: pct(user) } }), h('i.store', { style: { width: pct(store) } })),
    h(
      'div.cap-legend',
      h('span', h('i', { style: { background: 'var(--forge)' } }), `${t('sd.store')} ${fmtSize(store)}`),
      h('span', h('i', { style: { background: 'var(--mint)' } }), `${t('sd.userFile')} ${fmtSize(user)}`),
      h('span', h('i', { style: { background: '#9aa9b0' } }), `${t('sd.other')} ${fmtSize(other)}`),
    ),
  );
}
