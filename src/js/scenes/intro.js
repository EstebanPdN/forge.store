// Welcome intro, recreated from the Esteban's Forge site: the emblem (now a 3D
// coin) rises, "WELCOME / esteban's forge." arrives with 05-terminal.mp3, and
// pressing A plays forge-entry.mp3 while both screens slide up into the store as
// one sheet. The first time, A opens a light/dark picker with two 3D coins first.
import { Scene } from './scene.js';
import { h, animate, wait } from '../core/dom.js';
import { t } from '../core/i18n.js';
import { icon } from '../core/icons.js';
import { appIcon } from '../ui/components.js';
import { coin } from '../ui/coin.js';

const WELCOME = 'assets/audio/intro-welcome.mp3';
const ENTER = 'assets/audio/intro-enter.mp3';
const EASE = 'cubic-bezier(0.22, 1, 0.36, 1)';
const SPRING = 'cubic-bezier(0.34, 1.45, 0.64, 1)';
const ARRIVE = [
  { opacity: 0, transform: 'translateY(8px) scale(.97)' },
  { opacity: 1, transform: 'none' },
];

export class IntroScene extends Scene {
  static section = 'home';
  static noHome = true;
  static noToasts = true;

  showStatusbar() {
    return false;
  }

  build() {
    this.phase = 'welcome';
    this.halo = h('i.halo');
    const face = h('div.emblem-face', h('img', { src: 'assets/brand/forge-icon.png', alt: '' }));
    this.emblem = h('div.intro-emblem', this.halo, coin({ face, size: 76, depth: 12, edge: ['#ffffff', '#b9c4ca'] }));
    this.eyebrow = h('p.intro-eyebrow', 'WELCOME');
    this.title = h('h1.intro-title', 'esteban’s ', h('b', 'forge.'));
    this.welcomeTop = h('div.layer.intro-top', h('div.intro-panel', this.emblem, this.eyebrow, this.title));
    this.top.append(h('div.bg-forge'), this.welcomeTop);

    // Both versions use the base catalog here, independent of Universal-DB.
    const apps = this.catalog.data.apps;
    const half = Math.ceil(apps.length / 2);
    const row = (list, reverse) =>
      h('div.row' + (reverse ? '.reverse' : ''), [...list, ...list].map((app) => appIcon(app, { size: 40 })));
    this.tagline = h('div.intro-tagline', t('intro.tagline'));
    this.statusIcon = h('span.spinner');
    this.statusText = h('span', t('intro.connecting'));
    this.status = h('div.intro-status', this.statusIcon, this.statusText);
    this.marquee = h('div.intro-marquee', row(apps.slice(0, half)), row(apps.slice(half), true));
    this.enterBtn = h('button.intro-enter', { type: 'button' }, h('span', t('intro.enter')), icon('next'));
    this.bottomParts = [this.tagline, this.status, this.marquee, this.enterBtn];
    this.bottomParts.forEach((el) => el.classList.add('intro-hidden'));
    this.welcomeBottom = h('div.layer.intro-bottom', this.bottomParts);
    this.bottom.append(h('div.bg-forge'), this.welcomeBottom);
    this.bottom.addEventListener('click', (e) => this.tap(e));
  }

  onShow() {
    if (this.started) return;
    this.started = true;
    this.audio.stopMusic();
    this.audio.playFile(WELCOME, 0.5);

    animate(
      this.emblem,
      [
        { opacity: 0, transform: 'translateY(48px) scale(.82)', offset: 0 },
        { opacity: 1, transform: 'translateY(48px) scale(1)', offset: 0.3 },
        { opacity: 1, transform: 'translateY(48px) scale(1)', offset: 0.52 },
        { opacity: 1, transform: 'none', offset: 1 },
      ],
      { duration: 2600, easing: EASE, fill: 'backwards' },
    );
    animate(
      this.halo,
      [
        { opacity: 0, transform: 'scale(.6)' },
        { opacity: 1, transform: 'scale(1)', offset: 0.35 },
        { opacity: 0, transform: 'scale(1.5)' },
      ],
      { duration: 1700, delay: 350, fill: 'none' },
    );
    for (const el of [this.eyebrow, this.title]) {
      animate(el, ARRIVE, { duration: 1050, delay: 1650, easing: EASE, fill: 'backwards' });
    }

    const delays = [1700, 1850, 2000, 2550, 2850];
    this.bottomParts.forEach((el, i) => {
      el.classList.remove('intro-hidden');
      animate(el, ARRIVE, { duration: 900, delay: delays[i], easing: EASE, fill: 'backwards' });
    });

    this.timers = [
      setTimeout(() => (this.statusText.textContent = t('intro.reading')), 1300),
      setTimeout(() => {
        this.statusIcon.replaceWith((this.statusIcon = h('span.ok', icon('check'))));
        this.statusText.textContent = t('intro.ready', { n: this.catalog.data.apps.length });
        this.audio.sfx('toggle');
      }, 2350),
      setTimeout(() => (this.ready = true), 1100),
    ];
  }

  onButton(btn) {
    if (this.phase === 'picker') {
      if (btn === 'LEFT') this.choose('light');
      if (btn === 'RIGHT') this.choose('dark');
      if (btn === 'A' || btn === 'START') this.confirm();
      return;
    }
    if (btn === 'A' || btn === 'START') this.go();
  }

  tap(e) {
    if (this.phase !== 'picker') return this.go();
    const rect = this.bottom.getBoundingClientRect();
    const value = e.clientX - rect.left < rect.width / 2 ? 'light' : 'dark';
    if (value === this.choice) this.confirm();
    else this.choose(value);
  }

  destroy() {
    super.destroy();
    this.timers?.forEach(clearTimeout);
  }

  go() {
    if (this.leaving || !this.ready) return;
    if (!this.state.get('onboarded')) return this.showPicker();
    this.enterStore();
  }

  // --- first-run light / dark picker --------------------------------------------
  showPicker() {
    this.phase = 'picker';
    this.audio.sfx('select');
    animate(this.enterBtn, [{ transform: 'scale(1)' }, { transform: 'scale(.93)' }, { transform: 'scale(1)' }], { duration: 240, fill: 'none' });

    const option = (value, ic, edge) =>
      h(
        'div.theme-option',
        { dataset: { value } },
        coin({ face: h('div.theme-face.' + value, icon(ic)), size: 80, depth: 10, edge }),
        h('span.label', t('picker.' + value)),
      );
    this.options = [option('light', 'sun', ['#ffffff', '#d6ccb0']), option('dark', 'moon', ['#6b717d', '#1f2125'])];
    const title = h('h2.picker-title', t('picker.titleA'), h('b', t('picker.titleB')));
    const sub = h('div.picker-sub', t('picker.sub'));
    this.pickerTop = h('div.picker-top', title, sub, h('div.picker-options', this.options));
    this.pickerBottom = h(
      'div.layer.picker-bottom',
      h('div', t('picker.touch')),
    );
    this.top.append(this.pickerTop);
    this.bottom.append(this.pickerBottom);
    this.choice = this.state.get('theme') === 'dark' ? 'dark' : 'light';
    this.syncChoice();

    const hide = (el, frames) =>
      animate(el, frames, { duration: 360, easing: 'cubic-bezier(0.55, 0, 0.75, 0.2)', fill: 'forwards' }).then(() => (el.hidden = true));
    hide(this.welcomeTop, [{ opacity: 1, transform: 'none' }, { opacity: 0, transform: 'translateY(-14px) scale(.96)' }]);
    hide(this.welcomeBottom, [{ opacity: 1, transform: 'none' }, { opacity: 0, transform: 'translateY(10px)' }]);

    animate(title, ARRIVE, { duration: 700, delay: 280, easing: EASE, fill: 'backwards' });
    animate(sub, ARRIVE, { duration: 700, delay: 360, easing: EASE, fill: 'backwards' });
    this.options.forEach((el, i) => {
      animate(el.firstChild, [{ opacity: 0, transform: 'translateY(26px) scale(.4)' }, { opacity: 1, transform: 'none' }], {
        duration: 760,
        delay: 380 + i * 120,
        easing: SPRING,
        fill: 'backwards',
      });
      animate(el.lastChild, ARRIVE, { duration: 600, delay: 560 + i * 120, easing: EASE, fill: 'backwards' });
    });
    animate(this.pickerBottom, [{ opacity: 0 }, { opacity: 1 }], { duration: 600, delay: 600, fill: 'backwards' });
  }

  choose(value) {
    if (value === this.choice || this.leaving) return;
    this.choice = value;
    this.audio.sfx('move', { pitch: value === 'dark' ? -3 : 3 });
    this.syncChoice();
  }

  /** Previews the theme live on both screens while choosing. */
  syncChoice() {
    this.options.forEach((el) => el.classList.toggle('on', el.dataset.value === this.choice));
    document.documentElement.dataset.theme = this.choice;
  }

  async confirm() {
    if (this.leaving) return;
    this.leaving = true;
    this.state.set('theme', this.choice);
    this.state.set('onboarded', true);
    this.audio.sfx('toggle');
    const chosen = this.options.find((el) => el.dataset.value === this.choice);
    await animate(
      chosen.querySelector('.coin-body'),
      [{ transform: 'rotateY(0deg) scale(1)' }, { transform: 'rotateY(180deg) scale(1.16)', offset: 0.5 }, { transform: 'rotateY(360deg) scale(1)' }],
      { duration: 720, easing: 'cubic-bezier(0.45, 0, 0.2, 1)', fill: 'none' },
    );
    this.leaving = false;
    this.enterStore();
  }

  async enterStore() {
    if (this.leaving) return;
    this.leaving = true;
    this.audio.stopFile(WELCOME);
    this.audio.playFile(ENTER, 0.35);
    if (this.phase !== 'picker') {
      animate(this.enterBtn, [{ transform: 'scale(1)' }, { transform: 'scale(.93)' }, { transform: 'scale(1)' }], { duration: 240, fill: 'none' });
    }
    await wait(150);
    this.app.reset('home', { fromIntro: true }, 'dissolve');
  }

  /**
   * Leaving for the store: every part fades out with the arrive animation played
   * backwards (last in, first out), then the store builds itself up the same way.
   */
  async leave(how, next) {
    if (how !== 'dissolve') return super.leave(how, next);
    const parts =
      this.phase === 'picker'
        ? [...this.top.querySelectorAll('.picker-title, .picker-sub, .theme-option'), this.pickerBottom]
        : [this.emblem, this.eyebrow, this.title, ...this.bottomParts];
    await Promise.all(
      [...parts].reverse().map((el, i) =>
        animate(el, [...ARRIVE].reverse(), { duration: 420, delay: i * 55, easing: 'cubic-bezier(0.55, 0, 0.75, 0.2)', fill: 'forwards' }),
      ),
    );
  }
}
