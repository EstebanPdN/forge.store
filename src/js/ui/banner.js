// Layered "3D banner" for the top screen. Each app is composed from depth layers
// (background, middle, logo) that separate with the 3D slider. On 3DS every layer is
// drawn twice, shifted per eye by depth × osGet3DSliderState(); here the depth shows
// as parallax that follows the mouse and a slow idle sway.
import { h, animate } from '../core/dom.js';
import { glyphIcon } from '../core/icons.js';
import { FX } from '../core/fx.js';

export class Banner {
  constructor({ width = 400, height = 240, zoom = 1 } = {}) {
    this.canvas = h('canvas.bn-fx');
    this.slides = h('div.bn-slides');
    this.el = h('div.banner', this.slides, this.canvas, h('div.bn-vignette'));
    this.fx = new FX(this.canvas, width, height);
    this.fx.resize(zoom);
    this.current = null;
    this.px = 0;
    this.py = 0;
    this.tx = 0;
    this.ty = 0;
    this.lastPointer = 0;
    this.el.addEventListener('pointermove', (e) => this.pointer(e));
    this.el.addEventListener('pointerleave', () => (this.lastPointer = 0));
    this.loop = this.loop.bind(this);
    this.running = true;
    requestAnimationFrame(this.loop);
  }

  pointer(e) {
    const rect = this.el.getBoundingClientRect();
    this.tx = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    this.ty = ((e.clientY - rect.top) / rect.height) * 2 - 1;
    this.lastPointer = performance.now();
  }

  loop(now) {
    if (!this.running) return;
    if (now - this.lastPointer > 1800) {
      this.tx = Math.sin(now / 2600) * 0.7;
      this.ty = Math.cos(now / 3400) * 0.45;
    }
    this.px += (this.tx - this.px) * 0.06;
    this.py += (this.ty - this.py) * 0.06;
    this.el.style.setProperty('--px', this.px.toFixed(3));
    this.el.style.setProperty('--py', this.py.toFixed(3));
    requestAnimationFrame(this.loop);
  }

  pause() {
    this.running = false;
    this.fx.stop();
  }

  resume() {
    if (this.running) return;
    this.running = true;
    requestAnimationFrame(this.loop);
    if (this.app) this.fx.setPreset(this.app.banner?.fx);
  }

  destroy() {
    this.pause();
  }

  /** Builds the slide for an app. */
  compose(app) {
    const b = app.banner || {};
    const slide = h('div.bn-slide', { style: { '--tint': app.color || '#333' } });

    const bg = h('div.bn-bg');
    if (b.bg) {
      bg.style.backgroundImage = `url("${b.bg}")`;
      bg.style.backgroundPosition = b.bgPos || '50% 50%';
      if (b.cardIcon) bg.classList.add('gameplay');
      if (b.pixel) bg.classList.add('pixel');
    } else {
      const [c1, c2, c3] = b.gradient || [app.color, app.color, '#000'];
      bg.classList.add('generated');
      bg.style.background = `radial-gradient(120% 90% at 75% 15%, ${c3}66, transparent 60%), linear-gradient(160deg, ${c2}, ${c1})`;
    }
    slide.append(layer('bg', -0.7, bg));

    let mid = null;
    if (b.mid) mid = h('img.bn-mid-img', { src: b.mid, alt: '' });
    else if (!b.bg && !b.logo && typeof app.icon === 'object') {
      mid = h('div.bn-glyph', glyphIcon(app.icon, 132));
    } else if ((!b.bg || b.cardIcon) && typeof app.icon === 'string') {
      mid = h('img.bn-app-icon', { src: b.heroIcon || app.icon, alt: '', style: {background:app.iconBackground || ''} });
      slide.classList.add('has-app-icon');
    }
    if (mid) {
      slide.classList.add('has-mid');
      slide.append(layer('mid', 0.45, mid));
    }

    const logo = b.logo
      ? h('img.bn-logo-img' + (b.logoPixel ? '.pixel' : ''), { src: b.logo, alt: app.title, style: { '--ls': b.logoScale || 1, '--ly': `${b.logoY || 0}px` } })
      : h('div.bn-wordmark', h('span', app.title));
    slide.append(layer('logo', 1, logo));
    return slide;
  }

  /** Shows an app; direction = -1 / 1 slides like the grid moved, 0 fades. */
  show(app, direction = 0) {
    if (this.app === app) return;
    this.app = app;
    const old = this.current;
    const slide = this.compose(app);
    this.slides.append(slide);
    this.current = slide;
    this.fx.setPreset(app.banner?.fx);

    const dx = direction * 26;
    const parts = [...slide.querySelectorAll('.bn-in')];
    parts.forEach((part, i) => {
      const depth = [1, 1.8, 3][i] ?? 1;
      animate(
        part,
        [
          { opacity: 0, transform: `translateX(${dx * depth}px) scale(${i === parts.length - 1 ? 0.9 : 1.04})` },
          { opacity: 1, transform: 'none' },
        ],
        { duration: 620, delay: i * 70, easing: i === parts.length - 1 ? 'cubic-bezier(0.34, 1.4, 0.64, 1)' : 'cubic-bezier(0.22, 1, 0.36, 1)' },
      );
    });
    if (old) {
      const oldParts = [...old.querySelectorAll('.bn-in')];
      oldParts.forEach((part, i) =>
        animate(part, [{ opacity: 1 }, { opacity: 0, transform: `translateX(${-dx * ([1, 1.8, 3][i] ?? 1)}px)` }], { duration: 420, easing: 'cubic-bezier(0.4, 0, 1, 1)' }),
      );
      setTimeout(() => old.remove(), 450);
    }
  }
}

function layer(name, depth, content) {
  return h(`div.bn-layer.l-${name}`, { style: { '--d': depth } }, h('div.bn-in', content));
}
