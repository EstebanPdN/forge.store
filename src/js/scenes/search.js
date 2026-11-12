// Search: results on the top screen, a 3DS-style keyboard on the touch screen.
// The D-pad moves over the keys, A presses one, L/R pick a result and START opens
// it. A physical keyboard types straight into the field.
import { Scene } from './scene.js';
import { h, animate, clamp } from '../core/dom.js';
import { t, L } from '../core/i18n.js';
import { icon } from '../core/icons.js';
import { appIcon, Scroller } from '../ui/components.js';
import { forgeMode } from '../core/forge-mode.js';
import { ForgeSeal } from '../ui/forge-seal.js';

const LAYOUT = [
  ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0'],
  ['q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'],
  ['a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 'ñ'],
  ['z', 'x', 'c', 'v', 'b', 'n', 'm', '-', ['⌫', 2]],
  [[' ', 6], ['OK', 4]],
];

export class SearchScene extends Scene {
  static section = 'search';

  build() {
    this.secret = !!this.params.secret;
    this.query = '';
    this.pick = 0;
    this.row = 1;
    this.col = 0;

    this.queryEl = h('div.se-query');
    this.countEl = h('div.pn-sub');
    this.results = h('div.scroll-content.se-results');
    this.resultsView = h('div.se-results-view.scroller', this.results);
    this.scroller = new Scroller(this.resultsView, this.results);
    this.top.append(h('div.bg-forge'), h('div.pn-top', h('h2.pn-title', h('b', t('search.title'))), this.queryEl, this.countEl), this.resultsView);

    this.text = h('span.se-text');
    const input = h(
      'div.se-input',
      icon(this.secret ? 'shield' : 'search'),
      this.text,
      h('button.se-clear', { type: 'button', on: { click: () => this.setQuery('') } }, icon('close')),
    );
    this.keys = LAYOUT.map((row, r) =>
      row.map((spec, c) => {
        const [label, span] = Array.isArray(spec) ? spec : [spec, 1];
        const cls = span === 2 ? '.wide2' : span === 4 ? '.wide4' : span === 6 ? '.wide6' : '';
        const content = label === '⌫' ? icon('back') : label === ' ' ? t('search.space') : label === 'OK' ? (this.secret ? 'OK' : t('search.open')) : label;
        const key = h('button.key' + cls + (label === 'OK' ? '.ok' : ''), { type: 'button', dataset: { key: label } }, content);
        key.addEventListener('click', () => {
          this.row = r;
          this.col = c;
          this.syncFocus();
          this.press(label, key);
        });
        return key;
      }),
    );
    const board = h('div.kb', this.keys.map((row) => h('div.kb-row', row)));
    this.resultPosition = h('span');
    const hint = h('div.kb-hint',
      h('button.dt-back', { type: 'button', 'aria-label': t('action.back'), on: { click: () => this.app.back() } }, icon('back')),
      h('button.ui-btn.small', { type: 'button', 'aria-label': t('search.previous'), on: { click: () => this.movePick(-1) } }, icon('back')),
      this.resultPosition,
      h('button.ui-btn.small', { type: 'button', 'aria-label': t('search.next'), on: { click: () => this.movePick(1) } }, icon('next')),
    );
    this.bottom.append(h('div.bg-forge'), input, board, hint);
    this.render();
    this.syncFocus();
    if (this.secret) {
      this.top.classList.add('secret-code');
      this.top.querySelector('.pn-top').append(h('small.secret-eyebrow', 'FORGE MODE'), h('i.secret-progress'));
      this.bottom.querySelectorAll('.kb-hint > :not(:first-child)').forEach((el) => el.hidden = true);
      this.listen(forgeMode, 'change', (state) => {
        this.render();
        if (state === 'enabled') this.audio.sfx('complete');
        else if (state === 'invalid' || state === 'storage') this.audio.sfx('error');
      });
    }
  }

  onShow() {
    this.seal?.start();
    this.app.input.textHandler = (key) => {
      if (key === 'Enter') return this.openPick();
      if (key === 'Backspace') return this.press('⌫');
      if (/^[\p{L}\p{N} \-:.']$/u.test(key)) this.press(key.toLowerCase());
    };
  }

  onHide() {
    this.app.input.textHandler = null;
    this.seal?.stop();
    if (this.secret) this.query = '';
  }

  destroy() {
    super.destroy();
    this.seal?.stop();
    cancelAnimationFrame(this.scroller.raf);
    clearTimeout(this.scroller.snap);
    clearTimeout(this.scroller.flashTimer);
    if (this.app.input.textHandler) this.app.input.textHandler = null;
  }

  press(label, keyEl) {
    if (this.secret && (forgeMode.status === 'checking' || forgeMode.enabled)) return;
    const key = keyEl || this.keys.flat().find((k) => k.dataset.key === label);
    if (key) {
      key.classList.add('pressed');
      setTimeout(() => key.classList.remove('pressed'), 110);
    }
    if (label === 'OK') return this.openPick();
    if (label === '⌫') {
      if (!this.query) return this.audio.sfx('move', { pitch: -6 });
      this.audio.sfx('type', { pitch: -3 });
      return this.setQuery(this.query.slice(0, -1));
    }
    if (this.query.length >= 24) return this.audio.sfx('error');
    this.audio.sfx('type', { pitch: (label.charCodeAt(0) % 7) - 3 });
    this.setQuery(this.query + label);
  }

  setQuery(q) {
    this.query = q;
    this.pick = 0;
    this.render();
  }

  matches() {
    if (!this.query.trim()) return this.catalog.apps.filter((a) => a.featured || a.category === 'forge').filter((a) => a.status === 'released').slice(0, 5);
    return this.catalog.search(this.query);
  }

  render() {
    if (this.secret) {
      this.text.replaceChildren(...(this.query ? [this.query, h('i.caret')] : [h('i.caret'), h('span.ph', t('forge.title'))]));
      const state = forgeMode.status;
      if (!this.seal) { this.seal = new ForgeSeal(state); this.queryEl.replaceChildren(this.seal.el); }
      this.seal.set(state);
      this.top.dataset.forgeState = state;
      this.top.querySelector('.pn-title b').textContent = t(forgeMode.enabled ? 'forge.ready' : 'forge.title');
      this.countEl.textContent = t('forge.' + ({ locked: 'enter', enabled: 'enabled', checking: 'checking', invalid: 'invalid', storage: 'storage' }[state] || 'enter'));
      this.countEl.classList.toggle('forge-enabled', forgeMode.enabled);
      this.results.replaceChildren(); this.list = []; return;
    }
    this.text.replaceChildren(...(this.query ? [this.query, h('i.caret')] : [h('i.caret'), h('span.ph', t('search.placeholder'))]));
    this.queryEl.replaceChildren(this.query ? `«${this.query}»` : h('span.ph', t('search.suggestions')));
    const list = this.matches();
    this.list = list;
    this.pick = clamp(this.pick, 0, Math.max(0, list.length - 1));
    this.countEl.textContent = !this.query ? '' : list.length === 1 ? t('search.result') : list.length ? t('search.results', { n: list.length }) : t('search.none', { q: this.query });
    const visible = list.slice(0, 5);
    this.rowEls = visible.map((app, i) =>
      h(
        'div.se-row' + (i === this.pick ? '.focus' : ''),
        appIcon(app, { size: 26 }),
        h('span.li-text', h('b', app.title), h('small', `${app.author} · ${app.origin?.platform || ''} · ${L(this.catalog.categories.find((c) => c.id === app.category)?.label)}`)),
      ),
    );
    this.results.replaceChildren(...this.rowEls);
    this.scroller.reset();
    requestAnimationFrame(() => this.scroller.syncBar());
    this.syncPosition();
    this.rowEls.forEach((row, i) =>
      animate(row, [{ opacity: 0, transform: 'translateY(4px)' }, { opacity: 1, transform: 'none' }], { duration: 220, delay: i * 25, fill: 'backwards' }),
    );
  }

  movePick(dir) {
    const max = Math.min(5, this.list.length) - 1;
    if (max < 0) return;
    const next = clamp(this.pick + dir, 0, max);
    if (next === this.pick) return this.audio.sfx('move', { pitch: -5 });
    this.pick = next;
    this.audio.sfx('move');
    this.rowEls.forEach((row, i) => {
      row.classList.toggle('focus', i === next);
    });
    this.syncPosition();
    this.scroller.reveal(this.rowEls[next], 2);
  }

  syncPosition() {
    this.resultPosition.textContent = `${this.list.length ? this.pick + 1 : 0} / ${Math.min(5, this.list.length)}`;
  }

  openPick() {
    if (this.secret) { const code = this.query; this.setQuery(''); forgeMode.unlock(code); return; }
    const app = this.list[this.pick];
    if (!app) return this.audio.sfx('error');
    this.audio.sfx('select');
    this.app.open('detail', { app });
  }

  syncFocus() {
    this.keys.flat().forEach((k) => k.classList.remove('focus'));
    this.keys[this.row][this.col]?.classList.add('focus');
  }

  moveKey(btn) {
    const rows = this.keys;
    if (btn === 'LEFT' || btn === 'RIGHT') {
      this.col = clamp(this.col + (btn === 'RIGHT' ? 1 : -1), 0, rows[this.row].length - 1);
    } else {
      // Keep the horizontal position when moving between rows of different widths.
      const x = this.keys[this.row][this.col].offsetLeft + this.keys[this.row][this.col].offsetWidth / 2;
      this.row = clamp(this.row + (btn === 'DOWN' ? 1 : -1), 0, rows.length - 1);
      let best = 0;
      let bestDist = Infinity;
      rows[this.row].forEach((k, i) => {
        const d = Math.abs(k.offsetLeft + k.offsetWidth / 2 - x);
        if (d < bestDist) {
          best = i;
          bestDist = d;
        }
      });
      this.col = best;
    }
    this.audio.sfx('move', { pitch: 4 });
    this.syncFocus();
  }

  onButton(btn) {
    if (this.secret && btn === 'B') { this.query = ''; this.audio.sfx('back'); return this.app.back(); }
    if (this.secret && (btn === 'L' || btn === 'R')) return;
    if (['UP', 'DOWN', 'LEFT', 'RIGHT'].includes(btn)) return this.moveKey(btn);
    if (btn === 'A') return this.press(this.keys[this.row][this.col].dataset.key, this.keys[this.row][this.col]);
    if (btn === 'L' || btn === 'R') return this.movePick(btn === 'R' ? 1 : -1);
    if (btn === 'START') return this.openPick();
    if (btn === 'B') {
      if (this.query) return this.press('⌫');
      this.audio.sfx('back');
      return this.app.back();
    }
  }

  onLanguage() {
    this.top.querySelector('.pn-title b').textContent = t(this.secret ? (forgeMode.enabled ? 'forge.ready' : 'forge.title') : 'search.title');
    this.render();
  }
}
