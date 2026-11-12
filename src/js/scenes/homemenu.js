// HOME Menu: pressing HOME suspends the store (it shrinks away like on the console).
// From here it can be resumed, or closed — opening it again replays the intro.
// CIAs installed by the store show up here too.
import { Scene } from './scene.js';
import { h, animate, clamp } from '../core/dom.js';
import { t } from '../core/i18n.js';
import { icon } from '../core/icons.js';
import { appIcon, Scroller } from '../ui/components.js';
import { coin } from '../ui/coin.js';

const STORE = { id: 'store', title: 'forge.store', icon: 'assets/brand/forge-icon.png', pixelIcon: true };
const HBL = { id: 'hbl', title: 'Homebrew Launcher', icon: { glyph: 'box', bg: ['#5b6370', '#2a2f37'], fg: '#ffffff' } };

export class HomeMenuScene extends Scene {
  static section = 'homeMenu';
  static noHome = true;
  static handlesHome = true;

  showStatusbar() {
    return true;
  }

  build() {
    this.closed = false;
    this.focus = 0;
    const face = h('div.emblem-face', h('img', { src: 'assets/brand/forge-icon.png', alt: '' }));
    this.top.append(
      h('div.bg-forge'),
      h('div.hm-top', coin({ face, size: 70, depth: 11, edge: ['#ffffff', '#b9c4ca'] }), h('div.wordmark', 'esteban’s ', h('b', 'forge.')), (this.chip = h('span.chip.forge'))),
    );
    this.grid = h('div.hm-grid.scroll-content');
    this.gridView = h('div.hm-view.scroller', this.grid);
    this.scroller = new Scroller(this.gridView, this.grid);
    this.buttons = h('div.hm-buttons');
    this.hint = h('div.hm-hint');
    this.bottom.append(h('div.bg-forge'), this.gridView, this.buttons, this.hint);
    this.render();
    this.audio.stopMusic(true);
  }

  apps() {
    const cias = Object.entries(this.state.installed)
      .filter(([, record]) => record.cia)
      .map(([id]) => this.catalog.get(id))
      .filter(Boolean);
    return [STORE, HBL, ...cias];
  }

  render() {
    this.items = this.apps();
    this.focus = clamp(this.focus, 0, this.items.length - 1);
    this.tiles = this.items.map((app, i) =>
      h(
        'button.hm-app' + (i === this.focus ? '.focus' : ''),
        { type: 'button', on: { click: () => (i === this.focus ? this.launch() : this.setFocus(i)) } },
        appIcon(app, { size: 46 }),
        h('span.label', app.title),
        app === STORE && !this.closed ? h('span.suspended', '❚❚') : null,
      ),
    );
    this.grid.replaceChildren(...this.tiles);
    requestAnimationFrame(() => this.scroller.syncBar());
    this.chip.textContent = this.closed ? t('homeMenu.closed') : t('homeMenu.suspended');
    if (this.closed) {
      this.buttons.replaceChildren();
      this.hint.textContent = t('homeMenu.closed');
    } else {
      this.hint.textContent = '';
      this.buttons.replaceChildren(
        h('button.ui-btn.ghost', { type: 'button', on: { click: () => this.closeStore() } }, icon('close'), h('span', t('action.close'))),
        h('button.ui-btn.primary', { type: 'button', on: { click: () => this.resume() } }, icon('play'), h('span', t('action.resume'))),
      );
    }
  }

  setFocus(i) {
    i = clamp(i, 0, this.items.length - 1);
    if (i === this.focus) return;
    this.focus = i;
    this.tiles.forEach((el, k) => el.classList.toggle('focus', k === i));
    this.scroller.reveal(this.tiles[i], 4);
    this.audio.sfx('move', { pitch: i - 1 });
  }

  launch() {
    const app = this.items[this.focus];
    if (app === STORE) return this.closed ? this.reopen() : this.resume();
    this.audio.sfx('launch');
    this.app.toast({ text: t('toast.opening', { title: app.title }), image: appIcon(app, { size: 18 }) });
  }

  resume() {
    if (this.closed) return this.reopen();
    this.audio.sfx('open');
    this.app.back('suspend').then(() => this.audio.startMusic(1));
  }

  /** Closes the suspended store: everything below the HOME Menu is discarded. */
  closeStore() {
    if (this.closed) return;
    this.closed = true;
    this.audio.sfx('close');
    for (const scene of this.app.stack.slice(0, -1)) this.app.unmount(scene);
    this.app.stack = [this];
    this.app.syncChrome();
    animate(this.tiles[0], [{ transform: 'scale(1)' }, { transform: 'scale(.86)' }, { transform: 'scale(1)' }], { duration: 360, fill: 'none' });
    this.render();
  }

  reopen() {
    this.audio.sfx('select');
    this.app.reset('intro', {}, 'fade');
  }

  onButton(btn) {
    if (btn === 'LEFT' || btn === 'RIGHT') return this.setFocus(this.focus + (btn === 'RIGHT' ? 1 : -1));
    if (btn === 'UP' || btn === 'DOWN') return this.setFocus(this.focus + (btn === 'DOWN' ? 4 : -4));
    if (btn === 'A') return this.launch();
    if (btn === 'HOME') return this.closed ? this.reopen() : this.resume();
    if (btn === 'X') return this.closeStore();
  }

  onLanguage() {
    this.render();
  }

  destroy() {
    super.destroy();
    cancelAnimationFrame(this.scroller.raf);
    clearTimeout(this.scroller.snap);
    clearTimeout(this.scroller.flashTimer);
  }

  enter(how) {
    if (how !== 'suspend') return super.enter(how);
    return Promise.all([
      animate(this.top, [{ opacity: 0, transform: 'scale(1.06)' }, { opacity: 1, transform: 'none' }], { duration: 420 }),
      animate(this.bottom, [{ opacity: 0, transform: 'scale(1.06)' }, { opacity: 1, transform: 'none' }], { duration: 420 }),
    ]);
  }

  leave(how) {
    if (how !== 'suspend') return super.leave(how);
    return Promise.all([
      animate(this.top, [{ opacity: 1 }, { opacity: 0, transform: 'scale(1.04)' }], { duration: 300, easing: 'ease-in' }),
      animate(this.bottom, [{ opacity: 1 }, { opacity: 0, transform: 'scale(1.04)' }], { duration: 300, easing: 'ease-in' }),
    ]);
  }
}
