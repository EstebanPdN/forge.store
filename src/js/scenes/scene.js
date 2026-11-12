// Base class for screens of the store. A scene owns one layer on each screen
// and receives 3DS buttons while it is on top of the stack.
import { h, animate } from '../core/dom.js';
import { t } from '../core/i18n.js';

const OUT = 'cubic-bezier(0.22, 1, 0.36, 1)';
const IN = 'cubic-bezier(0.55, 0, 0.75, 0.2)';

export class Scene {
  static section = 'home';

  constructor(app, params = {}) {
    this.app = app;
    this.params = params;
    this.top = h('div.layer.scene-top');
    this.bottom = h('div.layer.scene-bottom');
    this.disposers = [];
  }

  get catalog() {
    return this.app.catalog;
  }
  get state() {
    return this.app.state;
  }
  get audio() {
    return this.app.audio;
  }
  get installer() {
    return this.app.installer;
  }

  build() {}
  showStatusbar() {
    return true;
  }
  sectionLabel() {
    return t('section.' + this.constructor.section);
  }
  onShow() {}
  onHide() {}
  onButton() {}
  onLanguage() {}

  listen(emitter, event, fn) {
    this.disposers.push(emitter.on(event, fn));
  }

  destroy() {
    this.disposers.forEach((dispose) => dispose());
    this.disposers = [];
  }

  // Default transitions: panels rise from the bottom like 3DS system applets.
  enter(how) {
    if (how === 'none') return Promise.resolve();
    if (how === 'fade') return fade(this, 0, 1, 380);
    return Promise.all([
      animate(this.top, [{ opacity: 0, transform: 'scale(1.035)' }, { opacity: 1, transform: 'none' }], { duration: 420, easing: OUT }),
      animate(this.bottom, [{ transform: 'translateY(100%)' }, { transform: 'none' }], { duration: 460, easing: OUT }),
    ]);
  }

  leave(how) {
    if (how === 'none') return Promise.resolve();
    if (how === 'fade') return fade(this, 1, 0, 280);
    return Promise.all([
      animate(this.top, [{ opacity: 1 }, { opacity: 0, transform: 'scale(1.02)' }], { duration: 260, easing: IN }),
      animate(this.bottom, [{ transform: 'none' }, { transform: 'translateY(100%)' }], { duration: 300, easing: IN }),
    ]);
  }

  cover(how) {
    if (how === 'none') return Promise.resolve();
    if (how === 'suspend') return suspend(this, true);
    return Promise.all([
      animate(this.top, [{ opacity: 1 }, { opacity: 0 }], { duration: 320, easing: OUT }),
      animate(this.bottom, [{ opacity: 1, transform: 'none' }, { opacity: 0, transform: 'scale(.94)' }], { duration: 360, easing: OUT }),
    ]);
  }

  uncover(how) {
    if (how === 'none') return Promise.resolve();
    if (how === 'suspend') return suspend(this, false);
    return Promise.all([
      animate(this.top, [{ opacity: 0 }, { opacity: 1 }], { duration: 360, easing: OUT }),
      animate(this.bottom, [{ opacity: 0, transform: 'scale(.94)' }, { opacity: 1, transform: 'none' }], { duration: 420, easing: OUT }),
    ]);
  }
}

/** HOME button: the running software shrinks away like on the console. */
export function suspend(scene, out) {
  const frames = [
    { opacity: 1, transform: 'none', borderRadius: '0px' },
    { opacity: 0, transform: 'scale(.72)', borderRadius: '14px' },
  ];
  const opts = { duration: out ? 380 : 460, easing: OUT };
  return Promise.all([scene.top, scene.bottom].map((el) => animate(el, out ? frames : [...frames].reverse(), opts)));
}

function fade(scene, from, to, duration) {
  return Promise.all(
    [scene.top, scene.bottom].map((el) => animate(el, [{ opacity: from }, { opacity: to }], { duration, easing: 'ease-in-out' })),
  );
}
