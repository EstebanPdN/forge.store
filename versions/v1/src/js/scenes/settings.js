// Settings: every row works with the D-pad (UP/DOWN to move, LEFT/RIGHT or A to
// change) and with the stylus. The top screen explains the focused option.
import { Scene } from './scene.js';
import { h, clamp, animate } from '../core/dom.js';
import { t, fmtDate, fmtSize, LANGUAGES } from '../core/i18n.js';
import { SD_CAPACITY } from '../core/sd.js';
import { icon } from '../core/icons.js';
import { glyph, Scroller } from '../ui/components.js';
import { coin } from '../ui/coin.js';
import { DEFAULTS } from '../core/state.js';

const OUT = 'cubic-bezier(0.22, 1, 0.36, 1)';
/** Settings that "Restore defaults" puts back. View and onboarding are left alone. */
const RESETTABLE = ['theme', 'music', 'sfx', 'volume', 'format', 'channel', 'universalDb'];

export class SettingsScene extends Scene {
  static section = 'settings';

  build() {
    this.focus = 0;
    this.hero = h('div.st-hero');
    this.about = h('div.st-about.pn-card');
    this.top.append(h('div.bg-forge'), h('div.pn-top', h('h2.pn-title', h('b', t('settings.title'))), this.hero), this.about);

    this.list = h('div.scroll-content.pn-list');
    this.scrollEl = h('div.pn-scroll.scroller', this.list);
    this.scroller = new Scroller(this.scrollEl, this.list);
    this.backBtn = h('button.dt-back', { type: 'button', on: { click: () => this.leaveScene() } }, icon('back'), glyph('B'));
    this.barTitle = h('div.pn-bar-title', t('settings.title'));
    this.bottom.append(h('div.bg-forge'), h('div.pn-bar', this.backBtn, this.barTitle), this.scrollEl);
    this.render();
    this.listen(this.state, 'setting', ({ key }) => key === 'volume' ? this.syncVolume() : this.render(true));
    this.listen(this.app.sd, 'change', () => this.render(true));
  }

  get dsp() {
    return this.catalog.data.globalRequirements.find((r) => r.id === 'dspfirm');
  }

  rows() {
    const model = this.state.get('console') === 'o3ds' ? 'Old 3DS' : 'New 3DS XL';
    const lang = LANGUAGES.find((l) => l.id === this.state.get('lang')) || LANGUAGES[0];
    const hasDsp = this.app.sd.exists(this.dsp.path);
    return [
      { id: 'console', icon: 'cpu', c: '#59606a', type: 'info', value: model },
      { id: 'theme', icon: this.state.get('theme') === 'dark' ? 'moon' : 'sun', c: '#f2a100', type: 'choice', options: [['light', t('settings.theme.light')], ['dark', t('settings.theme.dark')]] },
      { id: 'lang', icon: 'language', c: '#2f8fe0', type: 'list', value: lang.name, run: () => this.openLanguages() },
      { id: 'music', icon: 'music', c: '#7c6fd6', type: 'switch' },
      { id: 'sfx', icon: 'sparkle', c: '#9b6fd6', type: 'switch' },
      { id: 'volume', icon: 'volume', c: '#ff8a1f', type: 'stepper' },
      { id: 'format', icon: 'cia', c: '#4f9e86', type: 'choice', options: [['cia', 'CIA'], ['3dsx', '3DSX']] },
      { id: 'channel', icon: 'shield', c: '#7c6fd6', type: 'choice', options: [['stable', t('channel.stable')], ['experimental', t('channel.experimental')]] },
      { id: 'universalDb', label: 'settings.sources', icon: 'globe', c: '#2f8fe0', type: 'switch' },
      { id: 'dumpDsp', icon: 'volume', c: '#2f8fe0', type: 'action', value: hasDsp ? t('settings.dspFound') : t('settings.dspMissing'), ok: hasDsp, run: () => this.dumpDsp() },
      { id: 'reset', icon: 'refresh', c: '#e5484d', type: 'action', run: () => this.resetDefaults() },
    ];
  }

  value(row) {
    return row.get ? row.get() : this.state.get(row.id);
  }

  render(keepScroll = false) {
    this.items = this.rows();
    this.focus = clamp(this.focus, 0, this.items.length - 1);
    const y = this.scroller.y;
    this.rowEls = this.items.map((row, i) => this.renderRow(row, i));
    this.list.replaceChildren(...this.rowEls);
    if (keepScroll) this.scroller.scrollTo(y, false);
    requestAnimationFrame(() => this.scroller.syncBar());
    this.renderTop();
  }

  renderRow(row, i) {
    const value = this.value(row);
    let control;
    if (row.type === 'switch') control = h('span.switch' + (value ? '.on' : ''));
    else if (row.type === 'choice') {
      const option = row.options.find(([v]) => v === value) || row.options[0];
      control = [icon('back', 'arrow'), h('span.pill', option[1]), icon('next', 'arrow')];
    } else if (row.type === 'stepper') {
      const level = Math.round(value * 10);
      control = h('span.stepper',
        Array.from({ length: 10 }, (_, k) => h('i', { class: k < level ? 'on' : '', style: { height: `${4 + k * 0.9}px` } })),
        h('input.volume-range', { type: 'range', min: 0, max: 10, step: 1, value: level, 'aria-label': t('settings.volume'), on: {
          pointerdown: (e) => { e.stopPropagation(); cancelAnimationFrame(this.scroller.raf); this.scroller.v = 0; this.setFocus(i, false); },
          click: (e) => e.stopPropagation(),
          keydown: (e) => e.stopPropagation(),
          focus: () => this.setFocus(i, false),
          input: (e) => this.state.set('volume', Number(e.target.value) / 10),
        } }),
      );
    } else if (row.type === 'list') control = [h('span.pill', row.value), icon('next', 'arrow')];
    else if (row.type === 'info') control = h('span.info-value', row.value);
    else control = [row.value ? h('span.info-value' + (row.ok ? '.ok' : '.warn'), row.ok ? icon('check') : icon('warn'), row.value) : null, h('span.pill', t('settings.run'))];
    return h(
      (row.type === 'stepper' ? 'div.li' : 'button.li') + (i === this.focus ? '.focus' : '') + (row.type === 'info' ? '.static' : ''),
      {
        type: 'button',
        on: {
          click: () => {
            this.setFocus(i, false);
            this.change(1);
          },
        },
      },
      h('span.li-icon', { style: { '--c': row.c } }, icon(row.icon)),
      h('span.li-text', h('b', t(row.label || 'settings.' + row.id))),
      h('span.li-value', control),
    );
  }

  syncVolume() {
    const control = this.list.querySelector('.stepper');
    if (!control) return;
    const level = Math.round(this.state.get('volume') * 10);
    control.querySelectorAll('i').forEach((bar, i) => bar.classList.toggle('on', i < level));
    control.querySelector('input').value = level;
  }

  /** Everything about the console, shown when its row is focused. */
  consoleSheet() {
    const isNew = this.state.get('console') !== 'o3ds';
    const sd = this.app.sd;
    const installed = Object.keys(this.state.installed).length;
    const lang = LANGUAGES.find((l) => l.id === this.state.get('lang'));
    const battery = parseInt(this.app.statusbar.batteryPct.textContent, 10) || 0;
    const facts = [
      ['console.cpu', isNew ? 'ARM11 ×4 · 804 MHz' : 'ARM11 ×2 · 268 MHz'],
      ['console.ram', isNew ? '256 MB FCRAM' : '128 MB FCRAM'],
      ['console.gpu', 'PICA200 · 268 MHz'],
      ['console.vram', isNew ? '6 MB + 4 MB QTD' : '6 MB'],
      ['console.top', '400×240 · 3D 800×240'],
      ['console.bottom', '320×240 ' + t('console.touch')],
      ['console.firmware', '11.17.0-50E'],
      ['console.cfw', 'Luma3DS v13.3'],
      ['console.region', 'EUR'],
      ['console.language', lang ? lang.name : '—'],
      ['console.apps', String(installed)],
      ['console.battery', `${battery} %`],
    ];
    const used = sd.used();
    return h(
      'div.cs-sheet',
      h('div.cs-head', h('span.st-tile', { style: { '--c': '#59606a' } }, icon('cpu')), h('div', h('b', isNew ? 'New Nintendo 3DS XL' : 'Nintendo 3DS'), h('small', isNew ? 'KTR-001 · ' + t('console.c-stick') : 'CTR-001'))),
      h('div.cs-grid', facts.map(([k, v]) => h('div.cs-fact', h('span', t(k)), h('b', v)))),
      h(
        'div.cs-sd',
        h('div.head', h('span', t('console.sd')), h('b', t('downloads.free', { free: fmtSize(SD_CAPACITY - used), total: fmtSize(SD_CAPACITY) }))),
        h('div.cap-bar', h('i.store', { style: { width: `${(used / SD_CAPACITY) * 100}%` } })),
      ),
    );
  }

  renderTop() {
    const row = this.items[this.focus];
    const showConsole = row.id === 'console';
    this.top.classList.toggle('console-mode', showConsole);
    if (showConsole) {
      this.hero.replaceChildren(this.consoleSheet());
      return;
    }
    this.hero.replaceChildren(
      h('span.st-tile', { style: { '--c': row.c } }, icon(row.icon)),
      h('div', h('b', t(row.label || 'settings.' + row.id)), h('p', t('settings.desc.' + (row.label ? row.label.split('.').pop() : row.id)))),
    );
    const data = this.catalog.data;
    const face = h('div.emblem-face', h('img', { src: 'assets/brand/forge-icon.png', alt: '' }));
    const udb = this.state.get('universalDb');
    this.about.replaceChildren(
      coin({ face, size: 34, depth: 6, edge: ['#ffffff', '#b9c4ca'] }),
      h(
        'div',
        h('div.brand', h('b', 'forge.'), 'store'),
        h('small', t('settings.about', { rev: data.store.revision, n: this.catalog.apps.length, date: fmtDate(data.store.updated) })),
        h('div.sources', h('span.chip.mint', icon('check'), "Esteban's Forge"), h('span.chip' + (udb ? '.mint' : ''), icon(udb ? 'check' : 'globe'), 'Universal-DB')),
      ),
    );
  }

  setFocus(i, sound = true) {
    i = clamp(i, 0, this.items.length - 1);
    if (i === this.focus) return;
    this.rowEls[this.focus]?.classList.remove('focus');
    this.focus = i;
    this.rowEls[i].classList.add('focus');
    this.scroller.reveal(this.rowEls[i], 6);
    if (sound) this.audio.sfx('move');
    this.renderTop();
  }

  /** dir: +1 / -1 from the D-pad, +1 from A or a tap. */
  change(dir) {
    const row = this.items[this.focus];
    const value = this.value(row);
    if (row.type === 'info') return this.audio.sfx('move', { pitch: -5 });
    if (row.type === 'action' || row.type === 'list') {
      this.audio.sfx('select');
      return row.run();
    }
    this.audio.sfx('toggle');
    if (row.type === 'switch') return this.state.set(row.id, !value);
    if (row.type === 'stepper') {
      const next = Math.round(value * 10) + dir;
      return this.state.set(row.id, clamp(next, 0, 10) / 10);
    }
    const index = row.options.findIndex(([v]) => v === value);
    const next = row.options[(index + dir + row.options.length) % row.options.length][0];
    this.state.set(row.id, next);
  }

  /** Dumps the console's DSP firmware to /3ds/dspfirm.cdc (simulated, like DSP1). */
  async dumpDsp() {
    if (this.dumping) return;
    this.dumping = true;
    const dsp = this.dsp;
    const row = this.rowEls[this.focus];
    const value = row.querySelector('.li-value');
    value.replaceChildren(h('span.spinner'), h('span.info-value', t('settings.dspWorking')));
    await new Promise((r) => setTimeout(r, 1400));
    this.app.sd.writeFile(dsp.path, dsp.size, { kind: 'system', by: 'store' });
    this.state.save(this.app.sd);
    this.audio.sfx('complete');
    this.app.toast({ text: t('settings.dspDone'), icon: 'volume' });
    this.dumping = false;
    this.render(true);
  }

  resetDefaults() {
    for (const key of RESETTABLE) this.state.set(key, DEFAULTS[key]);
    this.app.toast({ text: t('settings.resetDone'), icon: 'refresh' });
  }

  // --- language list ----------------------------------------------------------------
  openLanguages() {
    if (this.langSheet) return;
    const current = this.state.get('lang');
    this.langFocus = Math.max(0, LANGUAGES.findIndex((l) => l.id === current));
    this.langRows = LANGUAGES.map((lang, i) =>
      h(
        'button.lang-row' + (lang.id === current ? '.on' : '') + (i === this.langFocus ? '.focus' : ''),
        { type: 'button', on: { click: () => this.pickLanguage(i) } },
        h('span.lang-name', lang.name),
        h('span.lang-en', lang.english),
        h('i.radio'),
      ),
    );
    const scrim = h('div.scrim', { on: { click: () => this.closeLanguages() } });
    const panel = h('div.sheet.lang-sheet', h('div.grabber'), h('h3', t('settings.lang')), h('div.lang-list', this.langRows));
    this.langSheet = h('div.sheet-wrap', scrim, panel);
    this.bottom.append(this.langSheet);
    animate(scrim, [{ opacity: 0 }, { opacity: 1 }], 280);
    animate(panel, [{ transform: 'translateY(110%)' }, { transform: 'none' }], { duration: 420, easing: OUT });
  }

  async closeLanguages() {
    const sheet = this.langSheet;
    if (!sheet) return;
    this.langSheet = null;
    const [scrim, panel] = sheet.children;
    animate(scrim, [{ opacity: 1 }, { opacity: 0 }], 240);
    await animate(panel, [{ transform: 'none' }, { transform: 'translateY(110%)' }], { duration: 260, easing: 'cubic-bezier(0.55, 0, 0.75, 0.2)' });
    sheet.remove();
  }

  pickLanguage(i) {
    this.audio.sfx('toggle');
    this.state.set('lang', LANGUAGES[i].id);
    this.closeLanguages();
  }

  langButton(btn) {
    if (btn === 'B') {
      this.audio.sfx('back');
      return this.closeLanguages();
    }
    if (btn === 'A') return this.pickLanguage(this.langFocus);
    if (btn === 'UP' || btn === 'DOWN') {
      const next = clamp(this.langFocus + (btn === 'DOWN' ? 1 : -1), 0, LANGUAGES.length - 1);
      if (next === this.langFocus) return;
      this.langFocus = next;
      this.langRows.forEach((r, k) => r.classList.toggle('focus', k === next));
      this.audio.sfx('move');
    }
  }

  leaveScene() {
    this.audio.sfx('back');
    this.app.back();
  }

  onButton(btn) {
    if (this.langSheet) return this.langButton(btn);
    if (btn === 'UP' || btn === 'DOWN') return this.setFocus(this.focus + (btn === 'DOWN' ? 1 : -1));
    if (btn === 'LEFT' || btn === 'RIGHT') {
      const row = this.items[this.focus];
      if (row.type === 'choice' || row.type === 'stepper' || row.type === 'switch') return this.change(btn === 'RIGHT' ? 1 : -1);
      return;
    }
    if (btn === 'A') return this.change(1);
    if (btn === 'B' || btn === 'START') return this.leaveScene();
  }

  onLanguage() {
    this.top.querySelector('.pn-title b').textContent = t('settings.title');
    this.barTitle.textContent = t('settings.title');
    this.render(true);
  }
}
