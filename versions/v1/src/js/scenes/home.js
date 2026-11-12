// Store home: the selected app's layered 3D banner on top, and the icon grid with
// category tabs, a gliding selection cursor and the toolbar on the touch screen.
import { Scene, suspend } from './scene.js';
import { h, animate, wait, clamp } from '../core/dom.js';
import { t, L, fmtSize, fmtStars } from '../core/i18n.js';
import { icon } from '../core/icons.js';
import { Banner } from '../ui/banner.js';
import { appIcon, glyph, statusChip } from '../ui/components.js';

const COLS = 5;
const ROWS = 3;
const PER_PAGE = COLS * ROWS;
const PAGE_W = 320;
const RING = 2 * Math.PI * 7.5; // small progress ring centred on the icon

export class HomeScene extends Scene {
  static section = 'home';

  build() {
    this.category = this.params.category || 'forge';
    // Coming from the intro, the status bar waits until the store has appeared.
    this.rising = !!this.params.fromIntro;
    this.index = 0;
    this.page = 0;

    // Top screen: banner + info card
    this.banner = new Banner({ zoom: this.app.zoom });
    this.card = h('div.home-card.glass');
    this.top.append(this.banner.el, this.card);
    this.onZoom = (e) => this.banner.fx.resize(e.detail);
    addEventListener('forge:zoom', this.onZoom);

    // Bottom screen: tabs, grid, dots, toolbar
    this.tabs = h('div.tabs-scroll');
    this.tabThumb = h('i.tab-thumb');
    this.tabsBar = h('div.home-tabs', glyph('L'), h('div.tabs-view', this.tabThumb, this.tabs), glyph('R'));
    this.track = h('div.grid-track');
    this.cursor = h('div.grid-cursor', h('i'));
    this.gridView = h('div.grid-view', this.track);
    this.dots = h('div.page-dots');
    this.toolbar = h('div.home-toolbar');
    this.bottom.append(h('div.bg-forge'), h('div.home-bottom', this.tabsBar, this.gridView, this.dots, this.toolbar));
    this.bindGrid();

    this.catalogRev = this.catalog.rev;
    this.renderTabs();
    this.renderToolbar();
    this.renderGrid();
    this.select(0, 0, true);

    this.listen(this.installer, 'progress', (job) => this.updateBadge(job.app));
    this.listen(this.installer, 'change', () => this.refreshStatuses());
    this.listen(this.installer, 'uninstall', () => this.refreshStatuses());
  }

  get list() {
    return this.catalog.list(this.category);
  }

  get pages() {
    return Math.max(1, Math.ceil(this.list.length / PER_PAGE));
  }

  syncCatalog() {
    const selected = this.gridApps?.[this.index]?.id;
    this.catalogRev = this.catalog.rev;
    if (!this.catalog.categories.some((c) => c.id === this.category)) this.category = 'forge';
    this.renderTabs();
    this.renderGrid();
    const index = this.list.findIndex((app) => app.id === selected);
    this.select(index >= 0 ? index : Math.min(this.index, this.list.length - 1), 0, true);
  }

  // --- tabs -----------------------------------------------------------------
  renderTabs() {
    this.tabs.replaceChildren(
      ...this.catalog.categories.map((cat) =>
        h(
          'button.tab',
          {
            type: 'button',
            dataset: { id: cat.id },
            'aria-pressed': String(cat.id === this.category),
            on: { click: () => this.setCategory(cat.id) },
          },
          icon(cat.icon),
          L(cat.label),
        ),
      ),
    );
    requestAnimationFrame(() => this.syncTabThumb(false));
  }

  syncTabThumb(animated = true) {
    const active = this.tabs.querySelector(`[data-id="${this.category}"]`);
    if (!active) return;
    this.tabs.querySelectorAll('.tab').forEach((b) => b.setAttribute('aria-pressed', String(b === active)));
    const viewW = this.tabs.parentElement.clientWidth;
    const maxShift = Math.max(0, this.tabs.scrollWidth - viewW);
    const shift = clamp(active.offsetLeft + active.offsetWidth / 2 - viewW / 2, 0, maxShift);
    this.tabs.style.transition = this.tabThumb.style.transition = animated ? '' : 'none';
    this.tabs.style.transform = `translateX(${-shift}px)`;
    const left = shift > 1 ? 'transparent, #000 12%' : '#000, #000';
    const right = shift < maxShift - 1 ? '#000 86%, transparent' : '#000, #000';
    this.tabs.parentElement.style.maskImage = `linear-gradient(90deg, ${left}, ${right})`;
    this.tabThumb.style.width = `${active.offsetWidth}px`;
    this.tabThumb.style.transform = `translateX(${active.offsetLeft - shift}px)`;
  }

  setCategory(id) {
    if (id === this.category) return;
    const ids = this.catalog.categories.map((c) => c.id);
    const dir = Math.sign(ids.indexOf(id) - ids.indexOf(this.category));
    this.category = id;
    this.audio.sfx('tab');
    this.syncTabThumb();
    this.renderGrid(dir);
    this.select(0, dir, true);
  }

  // --- grid -----------------------------------------------------------------
  renderGrid(dir = 0) {
    const list = this.list;
    this.gridApps = list;
    this.cells = [];
    const pages = [];
    for (let p = 0; p < this.pages; p++) {
      const page = h('div.grid-page');
      list.slice(p * PER_PAGE, (p + 1) * PER_PAGE).forEach((app, i) => {
        const index = p * PER_PAGE + i;
        const cell = h('div.cell', { dataset: { index } }, h('div.cell-in', appIcon(app)), h('div.badges'));
        cell.addEventListener('click', () => this.tap(index));
        this.cells.push(cell);
        page.append(cell);
      });
      if (!list.length) page.append(h('div.grid-empty', t('home.empty')));
      pages.push(page);
    }
    this.track.replaceChildren(...pages, this.cursor);
    this.page = 0;
    this.track.style.transition = 'none';
    this.track.style.transform = 'translateX(0)';
    this.refreshStatuses();
    this.renderDots();
    if (dir) {
      const cells = this.cells.slice(0, PER_PAGE);
      cells.forEach((cell, i) =>
        animate(cell, [{ opacity: 0, transform: `translateX(${dir * 26}px) scale(.9)` }, { opacity: 1, transform: 'none' }], {
          duration: 420,
          delay: (dir > 0 ? i % COLS : COLS - 1 - (i % COLS)) * 28 + Math.floor(i / COLS) * 18,
          fill: 'backwards',
        }),
      );
    }
  }

  renderDots() {
    this.dotActive = h('i.active');
    this.dots.replaceChildren(h('div.dots-in', ...Array.from({ length: this.pages }, () => h('i')), this.dotActive));
    this.dots.hidden = this.pages < 2;
    this.dotActive.style.transform = `translateX(${this.page * 9}px)`;
  }

  /** The coloured dot stretches towards the new page, then settles on it. */
  moveDot(from, to) {
    const dir = Math.sign(to - from);
    animate(
      this.dotActive,
      [
        { transform: `translateX(${from * 9}px)`, width: '5px' },
        { transform: `translateX(${(dir > 0 ? from : to) * 9}px)`, width: `${Math.abs(to - from) * 9 + 5}px`, offset: 0.5 },
        { transform: `translateX(${to * 9}px)`, width: '5px' },
      ],
      { duration: 420, fill: 'none' },
    );
    this.dotActive.style.transform = `translateX(${to * 9}px)`;
  }

  cellPos(index) {
    const local = index % PER_PAGE;
    const page = Math.floor(index / PER_PAGE);
    const col = local % COLS;
    const row = Math.floor(local / COLS);
    return { page, col, row, x: page * PAGE_W + 8 + col * 60.8 + 30.4, y: 1 + row * 56 + 28 };
  }

  select(index, dir = 0, instant = false) {
    const list = this.list;
    if (!list.length) {
      this.cursor.hidden = true;
      this.renderCard(null);
      return;
    }
    this.cursor.hidden = false;
    index = clamp(index, 0, list.length - 1);
    const changed = index !== this.index || instant;
    this.cells[this.index]?.classList.remove('selected');
    this.index = index;
    const cell = this.cells[index];
    cell.classList.add('selected');
    const pos = this.cellPos(index);
    this.cursor.style.transition = instant ? 'none' : '';
    this.cursor.style.transform = `translate(${pos.x - 30}px, ${pos.y - 30}px)`;
    if (pos.page !== this.page) this.goPage(pos.page);
    if (!changed) return;
    const app = list[index];
    this.renderCard(app);
    clearTimeout(this.bannerTimer);
    if (instant) this.banner.show(app, dir);
    else this.bannerTimer = setTimeout(() => this.banner.show(app, dir), 90);
  }

  goPage(page) {
    page = clamp(page, 0, this.pages - 1);
    if (page === this.page) return;
    const from = this.page;
    this.page = page;
    this.audio.sfx('page');
    this.track.style.transition = '';
    this.track.style.transform = `translateX(${-page * PAGE_W}px)`;
    this.moveDot(from, page);
  }

  move(btn) {
    const list = this.list;
    if (!list.length) return;
    const { page, col, row } = this.cellPos(this.index);
    let next = this.index;
    if (btn === 'LEFT') next = col > 0 ? this.index - 1 : page > 0 ? (page - 1) * PER_PAGE + row * COLS + COLS - 1 : this.index;
    if (btn === 'RIGHT') next = col < COLS - 1 ? this.index + 1 : page < this.pages - 1 ? (page + 1) * PER_PAGE + row * COLS : this.index;
    if (btn === 'UP') next = row > 0 ? this.index - COLS : this.index;
    if (btn === 'DOWN') next = row < ROWS - 1 ? this.index + COLS : this.index;
    next = clamp(next, 0, list.length - 1);
    if (next === this.index) {
      this.audio.sfx('move', { pitch: -5 });
      this.nudge(btn);
      return;
    }
    this.audio.sfx('move', { pitch: this.cellPos(next).col - 2 });
    this.select(next, Math.sign(next - this.index));
  }

  /** Small bump of the cursor at the edges. */
  nudge(btn) {
    const d = { LEFT: [-4, 0], RIGHT: [4, 0], UP: [0, -4], DOWN: [0, 4] }[btn];
    animate(this.cursor.firstChild, [{ transform: 'none' }, { transform: `translate(${d[0]}px, ${d[1]}px)` }, { transform: 'none' }], {
      duration: 220,
      fill: 'none',
    });
  }

  tap(index) {
    if (index === this.index) this.openDetail();
    else {
      this.audio.sfx('move', { pitch: this.cellPos(index).col - 2 });
      this.select(index, Math.sign(index - this.index));
    }
  }

  bindGrid() {
    let startX = null;
    let startT = 0;
    this.gridView.addEventListener('pointerdown', (e) => {
      startX = e.clientX;
      startT = performance.now();
    });
    this.gridView.addEventListener('pointerup', (e) => {
      if (startX == null) return;
      const zoom = this.gridView.getBoundingClientRect().width / this.gridView.offsetWidth;
      const dx = (e.clientX - startX) / zoom;
      startX = null;
      if (Math.abs(dx) > 40 && performance.now() - startT < 600) {
        const dir = dx < 0 ? 1 : -1;
        const target = clamp(this.page + dir, 0, this.pages - 1);
        if (target === this.page) return;
        const { col, row } = this.cellPos(this.index);
        this.gridView.addEventListener('click', (ev) => ev.stopPropagation(), { capture: true, once: true });
        this.select(Math.min(this.list.length - 1, target * PER_PAGE + row * COLS + col), dir);
      }
    });
  }

  // --- statuses and badges ----------------------------------------------------
  refreshStatuses() {
    this.list.forEach((app, i) => this.renderBadges(app, this.cells[i]));
    if (this.list[this.index]) this.renderCard(this.list[this.index], true);
    this.renderToolbar();
  }

  updateBadge(app) {
    const i = this.list.indexOf(app);
    if (i < 0) return;
    const ring = this.cells[i]?.querySelector('.ring circle.p');
    const job = this.installer.find(app.id);
    if (ring && job) ring.style.strokeDashoffset = String(RING * (1 - job.progress));
    else this.renderBadges(app, this.cells[i]);
    if (i === this.index) this.renderCardStatus(app);
    this.renderToolbar();
  }

  renderBadges(app, cell) {
    if (!cell) return;
    const status = this.catalog.status(app, this.installer.jobs);
    const badges = cell.querySelector('.badges');
    badges.replaceChildren();
    cell.dataset.status = status.kind;
    if (status.kind === 'installed') badges.append(h('i.badge.ok', icon('check')));
    if (status.kind === 'update') badges.append(h('i.badge.up', icon('update')));
    if (status.kind === 'needs-file') badges.append(h('i.badge.warn', icon('warn')));
    if (status.kind === 'dev') badges.append(h('i.badge.dev', icon('hammer')));
    if (status.kind === 'downloading' || status.kind === 'queued') {
      const progress = status.job.progress || 0;
      badges.append(
        h('div.ring', {
          html: `<svg viewBox="0 0 22 22"><circle class="bgc" cx="11" cy="11" r="10.5"/><circle cx="11" cy="11" r="7.5"/><circle class="p" cx="11" cy="11" r="7.5" style="stroke-dashoffset:${RING * (1 - progress)}"/></svg>`,
        }),
      );
      if (status.kind === 'queued') badges.append(h('i.badge.queue', icon('clock')));
    }
  }

  // --- info card on the top screen ---------------------------------------------
  renderCard(app, quiet = false) {
    if (!app) {
      this.card.replaceChildren();
      return;
    }
    const release = this.catalog.release(app);
    const chips = [];
    if (release) chips.push(h('span.chip' + (release.channel === 'experimental' ? '.lav' : '.mint'), `${release.tag}`));
    if (app.requires?.some((r) => r.id === 'rom')) chips.push(h('span.chip', icon('rom'), t('chip.rom')));
    else if (app.requires?.length) chips.push(h('span.chip', icon('file'), t('chip.data')));
    if (app.compat?.o3ds === false) chips.push(h('span.chip', t('chip.n3dsOnly')));
    if (app.compat?.stereo) chips.push(h('span.chip', icon('depth'), t('chip.3d')));

    this.cardIcon = appIcon(app, { size: 40 });
    this.cardStatus = h('div.card-status');
    const content = h(
      'div.card-content',
      this.cardIcon,
      h(
        'div.card-text',
        h('div.card-title.ellipsis', app.title),
        h('div.card-sub.ellipsis', t('home.by', { author: app.author }), app.origin?.platform ? ` · ${app.origin.platform} → 3DS` : ''),
        h('div.card-chips', chips),
      ),
      this.cardStatus,
    );
    if (!quiet && this.card.firstChild) {
      animate(content, [{ opacity: 0, transform: 'translateY(4px)' }, { opacity: 1, transform: 'none' }], { duration: 260, fill: 'none' });
    }
    this.card.replaceChildren(content);
    this.renderCardStatus(app);
  }

  renderCardStatus(app) {
    if (!this.cardStatus || this.list[this.index] !== app) return;
    const status = this.catalog.status(app, this.installer.jobs);
    const stars = app.stars != null ? h('span.card-stars', icon('star'), fmtStars(app.stars)) : null;
    let main;
    if (status.kind === 'downloading' || status.kind === 'queued') {
      const p = status.job.progress || 0;
      main = h('div.card-progress', h('span', status.kind === 'queued' ? t('status.queued') : `${Math.round(p * 100)} %`), h('div.bar', h('i', { style: { '--p': p } })));
    } else if (status.kind === 'dev') {
      main = h('div.card-progress', h('span', `${app.progress} %`), h('div.bar.static', h('i', { style: { '--p': app.progress / 100, background: 'var(--lavender)' } })));
    } else {
      main = statusChip(status) || h('span.chip.forge.size', icon('download'), fmtSize(this.catalog.downloadSize(app)));
    }
    this.cardStatus.replaceChildren(stars || '', main);
  }

  // --- toolbar ----------------------------------------------------------------
  renderToolbar() {
    const active = this.installer.active().length;
    // Progress events fire many times a second: only rebuild when something changes.
    const key = `${active}|${this.app.state.get('lang')}`;
    if (key === this.toolbarKey) return;
    this.toolbarKey = key;
    const tool = (key, ic, btn, open, extra) =>
      h('button.tool', { type: 'button', on: { click: () => open() } }, icon(ic), h('span', t(key)), extra, glyph(btn));
    this.toolbar.replaceChildren(
      tool('home.search', 'search', 'Y', () => this.openScene('search')),
      tool('home.downloads', 'download', 'X', () => this.openScene('downloads'), active ? h('b.count', String(active)) : null),
      tool('home.sd', 'sd', 'SELECT', () => this.openScene('sd')),
      tool('home.settings', 'settings', 'START', () => this.openScene('settings')),
      h('button.tool.round', { type: 'button', title: 'GitHub', on: { click: () => this.openProfile() } }, icon('github')),
    );
  }

  /** The author's own GitHub page, loaded live from the public API. */
  openProfile() {
    this.audio.sfx('open');
    this.app.open('github', { user: this.catalog.data.store.author });
  }

  openScene(name) {
    this.audio.sfx('open');
    this.app.open(name);
  }

  openDetail() {
    const app = this.list[this.index];
    if (!app) return;
    this.audio.sfx('select');
    this.app.open('detail', { app }, 'zoom');
  }

  // --- input ------------------------------------------------------------------
  onButton(btn) {
    switch (btn) {
      case 'UP':
      case 'DOWN':
      case 'LEFT':
      case 'RIGHT':
        return this.move(btn);
      case 'A':
        return this.openDetail();
      case 'L':
      case 'R': {
        const ids = this.catalog.categories.map((c) => c.id);
        const next = ids[(ids.indexOf(this.category) + (btn === 'R' ? 1 : ids.length - 1)) % ids.length];
        return this.setCategory(next);
      }
      case 'Y':
        return this.openScene('search');
      case 'X':
        return this.openScene('downloads');
      case 'SELECT':
        return this.openScene('sd');
      case 'START':
        return this.openScene('settings');
      default:
    }
  }

  onLanguage() {
    if (this.catalogRev !== this.catalog.rev) this.syncCatalog();
    this.renderTabs();
    this.toolbarKey = null;
    this.renderToolbar();
    this.renderCard(this.list[this.index], true);
    this.cells.forEach((cell) => cell.querySelector('.grid-empty')?.replaceChildren(t('home.empty')));
  }

  onShow() {
    // A source was added or removed (Universal-DB) while another screen was open.
    if (this.catalogRev !== this.catalog.rev) this.syncCatalog();
    this.banner.resume();
    this.audio.muffle(0);
    if (!this.musicStarted) {
      this.musicStarted = true;
      setTimeout(() => this.audio.startMusic(this.params.fromIntro ? 3 : 1.5), this.params.fromIntro ? 400 : 0);
    }
    this.refreshStatuses();
  }

  onHide() {
    setTimeout(() => {
      if (this.top.hidden) this.banner.pause();
    }, 600);
  }

  destroy() {
    super.destroy();
    this.banner.destroy();
    removeEventListener('forge:zoom', this.onZoom);
  }

  // --- transitions --------------------------------------------------------------
  showStatusbar() {
    return !this.rising;
  }

  async enter(how, prev) {
    if (how === 'dissolve') {
      this.rising = true;
      // The intro needs ~810 ms to fade out completely; the store starts after it.
      const start = 900;
      const arrive = [
        { opacity: 0, transform: 'translateY(8px) scale(.97)' },
        { opacity: 1, transform: 'none' },
      ];
      const opts = (delay, duration = 700) => ({ duration, delay: start + delay, easing: 'cubic-bezier(0.22, 1, 0.36, 1)', fill: 'backwards' });
      animate(this.top, [{ opacity: 0 }, { opacity: 1 }], opts(0, 500));
      animate(this.bottom, [{ opacity: 0 }, { opacity: 1 }], opts(0, 1));
      animate(this.card, arrive, opts(160));
      animate(this.tabsBar, arrive, opts(60));
      this.cells.slice(this.page * PER_PAGE, this.page * PER_PAGE + PER_PAGE).forEach((cell, i) =>
        animate(cell, arrive, opts(140 + i * 35)), // the whole cell, so badges arrive with their icon
      );
      animate(this.cursor, [{ opacity: 0 }, { opacity: 1 }], opts(560, 400));
      animate(this.dots, arrive, opts(600));
      animate(this.toolbar, arrive, opts(640));
      await wait(start + 1300);
      this.rising = false;
      this.app.syncChrome();
      return;
    }
    if (how === 'rise') {
      this.rising = true;
      await wait(1300);
      this.rising = false;
      this.app.syncChrome();
      this.land();
      return;
    }
    return super.enter(how, prev);
  }

  /**
   * The store arrives already visible with the slide, so nothing fades in again
   * (that caused a double blink). It only lands: a light wave across the icons.
   */
  land() {
    const visible = this.cells.slice(this.page * PER_PAGE, this.page * PER_PAGE + PER_PAGE);
    visible.forEach((cell, i) => {
      const icon = cell.querySelector('.app-icon');
      const wave = (i % COLS) + Math.floor(i / COLS);
      animate(icon, [{ transform: 'none' }, { transform: 'translateY(-4px) scale(1.07)', offset: 0.45 }, { transform: 'none' }], {
        duration: 560,
        delay: wave * 55,
        easing: 'cubic-bezier(0.34, 1.3, 0.64, 1)',
        fill: 'none',
      });
    });
    animate(this.cursor.firstChild, [{ transform: 'scale(.86)' }, { transform: 'scale(1.06)', offset: 0.6 }, { transform: 'none' }], {
      duration: 520,
      delay: 120,
      fill: 'none',
    });
  }

  cover(how, next) {
    if (how === 'zoom') {
      return Promise.all([
        animate(this.top, [{ opacity: 1, transform: 'none' }, { opacity: 0, transform: 'scale(1.06)' }], { duration: 420 }),
        animate(this.bottom, [{ opacity: 1, transform: 'none' }, { opacity: 0, transform: 'scale(.92)' }], { duration: 360 }),
      ]);
    }
    if (how === 'suspend') return suspend(this, true);
    return super.cover(how, next);
  }

  uncover(how, prev) {
    if (how === 'zoom') {
      return Promise.all([
        animate(this.top, [{ opacity: 0, transform: 'scale(1.06)' }, { opacity: 1, transform: 'none' }], { duration: 480 }),
        animate(this.bottom, [{ opacity: 0, transform: 'scale(.92)' }, { opacity: 1, transform: 'none' }], { duration: 460 }),
      ]);
    }
    if (how === 'suspend') return suspend(this, false);
    return super.uncover(how, prev);
  }
}
