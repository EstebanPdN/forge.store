// Scene stack, input routing and the persistent chrome of both screens
// (status bar, toasts, stylus ripples, download LED).
import { h } from './core/dom.js';
import { t, L } from './core/i18n.js';
import { StatusBar, Toasts, ripple, appIcon } from './ui/components.js';

export class App {
  constructor({ device, top, bottom, catalog, state, sd, installer, audio, input }) {
    Object.assign(this, { device, catalog, state, sd, installer, audio, input });
    this.screens = { top, bottom };
    this.layers = { top: h('div.layer.scenes'), bottom: h('div.layer.scenes') };
    top.append(this.layers.top);
    bottom.append(this.layers.bottom);
    this.statusbar = new StatusBar(top);
    this.toasts = new Toasts(top);
    this.stack = [];
    this.busy = false;
    this.scenes = {};

    input.handler = (btn, info) => this.button(btn, info);

    // Stylus feedback on the touch screen; the top screen ignores clicks, like hardware.
    bottom.addEventListener('pointerdown', (e) => ripple(bottom, e.clientX, e.clientY));
    top.addEventListener('click', (e) => e.stopPropagation(), true);

    installer.on('progress', () => this.syncNet());
    installer.on('step', () => this.syncNet());
    installer.on('change', () => this.syncNet());
    installer.on('done', (job) => this.jobDone(job));
  }

  get scene() {
    return this.stack[this.stack.length - 1];
  }

  /** Registers scene classes so scenes can open each other by name. */
  register(map) {
    Object.assign(this.scenes, map);
  }

  mount(scene) {
    scene.build();
    this.layers.top.append(scene.top);
    this.layers.bottom.append(scene.bottom);
  }

  unmount(scene) {
    scene.destroy();
    scene.top.remove();
    scene.bottom.remove();
  }

  async run(fn) {
    if (this.busy) return false;
    this.busy = true;
    try {
      await fn();
    } finally {
      this.busy = false;
    }
    if (this.pendingToasts?.length && !this.scene?.constructor.noToasts) this.flushToasts();
    return true;
  }

  open(name, params = {}, how = 'sheet') {
    const SceneClass = typeof name === 'string' ? this.scenes[name] : name;
    return this.run(async () => {
      const prev = this.scene;
      const scene = new SceneClass(this, params);
      this.mount(scene);
      prev?.onHide();
      this.stack.push(scene);
      this.syncChrome();
      await Promise.all([prev?.cover(how, scene), scene.enter(how, prev)]);
      if (prev) {
        prev.top.hidden = prev.bottom.hidden = true;
        clean(prev);
      }
      clean(scene);
      scene.onShow();
    });
  }

  back(how = 'sheet') {
    if (this.stack.length < 2) return Promise.resolve(false);
    return this.run(async () => {
      const scene = this.stack.pop();
      const prev = this.scene;
      scene.onHide();
      prev.top.hidden = prev.bottom.hidden = false;
      this.syncChrome();
      await Promise.all([scene.leave(how, prev), prev.uncover(how, scene)]);
      this.unmount(scene);
      clean(prev);
      prev.onShow();
    });
  }

  /** Replaces the whole stack (intro → store, closing from the HOME Menu). */
  reset(name, params = {}, how = 'fade') {
    const SceneClass = typeof name === 'string' ? this.scenes[name] : name;
    return this.run(async () => {
      const old = [...this.stack];
      const scene = new SceneClass(this, params);
      this.mount(scene);
      this.stack = [scene];
      this.syncChrome();
      const current = old[old.length - 1];
      current?.onHide();
      await Promise.all([current?.leave(how, scene), scene.enter(how, current)]);
      old.forEach((s) => this.unmount(s));
      clean(scene);
      scene.onShow();
    });
  }

  button(btn, info) {
    if (this.busy) return;
    if (btn === 'HOME') {
      if (this.scene?.constructor.handlesHome) return this.scene.onButton('HOME');
      if (this.scene && !this.scene.constructor.noHome) this.openHomeMenu();
      return;
    }
    this.scene?.onButton(btn, info);
  }

  openHomeMenu() {
    this.audio.sfx('close');
    return this.open('homeMenu', {}, 'suspend');
  }

  syncChrome() {
    const scene = this.scene;
    this.statusbar.show(!!scene && scene.showStatusbar());
    this.statusbar.setSection(scene ? scene.sectionLabel() : '');
    this.syncNet();
  }

  syncNet() {
    const running = this.installer.running?.status === 'running' ? this.installer.running : null;
    const downloading = running?.steps.some((s) => s.status === 'active' && (s.type === 'downloadRelease' || s.type === 'resolve'));
    this.device.classList.toggle('net', !!downloading);
    this.statusbar.setDownloading(!!running);
  }

  toast(opts) {
    if (this.busy || !this.scene || this.scene.constructor.noToasts) {
      (this.pendingToasts ||= []).push(opts);
      return;
    }
    this.toasts.show(opts);
  }

  flushToasts() {
    const queue = this.pendingToasts || [];
    this.pendingToasts = [];
    queue.forEach((opts, i) => setTimeout(() => this.toast(opts), 400 + i * 450));
  }

  jobDone(job) {
    if (job.status !== 'done') {
      if (job.status === 'cancelled') this.toast({ text: t('toast.cancelled'), icon: 'close', tone: 'danger' });
      return;
    }
    const watching = this.scene?.job === job;
    if (!watching) {
      this.audio.sfx('complete');
      this.toast({
        text: job.missing.length ? t('toast.needsFile') + ' · ' + job.app.title : t('toast.installed', { title: job.app.title }),
        image: appIcon(job.app, { size: 18 }),
        tone: job.missing.length ? 'amber' : 'mint',
        ms: 3200,
      });
    }
  }

  /** Called when the language changes so visible scenes can re-render text. */
  relabel() {
    this.syncChrome();
    this.stack.forEach((scene) => scene.onLanguage());
  }

  localize(value) {
    return L(value);
  }
}

/** Drops finished fill animations so later CSS and animations start clean. */
function clean(scene) {
  for (const el of [scene.top, scene.bottom]) el.getAnimations().forEach((a) => a.cancel());
}
