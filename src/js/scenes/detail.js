// App page: screenshot carousel and header on top; Info, Screens, Install and
// What's-new tabs plus the action bar on the touch screen. The Install tab
// previews every folder and file the script will create on the SD card.
import { Scene } from './scene.js';
import { h, animate, clamp } from '../core/dom.js';
import { t, L, fmtSize, fmtDate, fmtStars } from '../core/i18n.js';
import { icon } from '../core/icons.js';
import { Banner } from '../ui/banner.js';
import { appIcon, glyph, segmented, Scroller } from '../ui/components.js';
import { TabBar, revealTabContent } from '../ui/navigation.js';
import { baseName, parentOf, splitPath, normalize } from '../core/sd.js';

const fmtTime = (s) => `${Math.floor((s || 0) / 60)}:${String(Math.floor((s || 0) % 60)).padStart(2, '0')}`;

const TABS = ['info', 'shots', 'install', 'news'];
const OUT = 'cubic-bezier(0.22, 1, 0.36, 1)';

export class DetailScene extends Scene {
  static section = 'detail';

  showStatusbar() {
    return this.playing ? !!this.videoUI : this.tab !== 'shots';
  }

  build() {
    this.item = this.params.app;
    this.tab = this.params.tab || 'info';
    this.format = this.catalog.format(this.item);
    this.channel = this.catalog.release(this.item)?.channel;
    this.buildTop();
    this.buildBottom();
    this.listen(this.installer, 'progress', (job) => job.app === this.item && this.renderAction());
    this.listen(this.installer, 'change', () => this.renderAction());
    this.listen(this.installer, 'uninstall', () => this.refresh());
    this.listen(this.installer, 'done', (job) => job.app === this.item && this.refresh());
    this.listen(this.app.sd, 'change', () => this.tab === 'install' && !this.sheet && this.renderTab(true));
  }

  refresh() {
    this.renderAction();
    this.renderChips();
    if (this.tab === 'install' || this.tab === 'info') this.renderTab(true);
  }

  // --- top screen -------------------------------------------------------------
  buildTop() {
    const item = this.item;
    this.hero = h('div.dt-hero');
    // Videos first (trailers, gameplay), then screenshots.
    this.mediaList = [
      ...(item.videos || []).map((v) => ({ type: 'video', ...v })),
      ...(item.shots || []).map((s) => ({ type: 'shot', ...s })),
    ];
    this.mediaIndex = Math.max(0, this.mediaList.findIndex((m) => m.type === 'shot'));
    if (this.mediaList.length) {
      this.heroEls = this.mediaList.map((m) =>
        m.type === 'video'
          ? h('div.dt-shot.poster', { style: { backgroundImage: `url("${m.poster}")` } }, h('i.dt-play-badge', icon('play')))
          : h('div.dt-shot', { style: { backgroundImage: `url("${m.top}")` } }),
      );
      this.heroEls[this.mediaIndex].classList.add('on');
      this.videoTop = h('video.dt-video', { playsinline: true, preload: 'none' });
      this.hero.append(...this.heroEls, this.videoTop);
      this.counter = h('div.dt-counter');
      this.syncCounter();
    } else {
      this.banner = new Banner({ zoom: this.app.zoom });
      this.banner.show(item, 0);
      this.hero.append(this.banner.el);
    }
    this.headIcon = appIcon(item, { size: 46 });
    this.chips = h('div.dt-chips');
    const origin = item.origin ? `${item.origin.platform}${item.origin.year ? ' · ' + item.origin.year : ''} → 3DS` : '';
    this.head = h(
      'div.dt-head',
      this.headIcon,
      h('div.dt-head-text', h('h2.ellipsis', item.fullTitle || item.title), h('div.dt-meta.ellipsis', t('home.by', { author: item.author }), origin ? ' · ' + origin : ''), this.chips),
    );
    this.top.append(this.hero, h('div.dt-shade'), this.counter || '', this.head);
    this.renderChips();
  }

  renderChips() {
    const item = this.item;
    const rel = this.catalog.release(item, this.channel);
    const chips = [];
    if (rel) chips.push(h('span.chip.dark', rel.channel === 'experimental' ? icon('sparkle') : icon('shield'), `${rel.tag} · ${t('channel.' + rel.channel)}`));
    if (item.stars != null) chips.push(h('span.chip.dark', icon('star'), fmtStars(item.stars)));
    if (item.compat?.o3ds === false) chips.push(h('span.chip.dark', t('chip.n3dsOnly')));
    if (item.compat?.stereo) chips.push(h('span.chip.dark', icon('depth'), item.compat.stereo === 'experimental' ? '3D β' : '3D'));
    if (this.catalog.needsRom(item)) chips.push(h('span.chip.dark', icon('rom'), t('chip.rom')));
    const status = this.catalog.status(item, this.installer.jobs);
    if (status.kind === 'installed') chips.unshift(h('span.chip.mint', icon('check'), t('status.installed')));
    if (status.kind === 'needs-file') chips.unshift(h('span.chip.amber', icon('warn'), t('status.needsFile')));
    if (status.kind === 'dev') chips.unshift(h('span.chip.lav', icon('hammer'), `${t('status.dev')} · ${item.progress} %`));
    this.chips.replaceChildren(...chips);
  }

  syncCounter() {
    if (!this.counter) return;
    const m = this.mediaList[this.mediaIndex];
    const label = m.type === 'video' ? `▶ ${L(m.title)} · ${fmtTime(m.duration)}` : L(m.caption);
    this.counter.replaceChildren(
      h('span.chip.dark', `${this.mediaIndex + 1}/${this.mediaList.length}`, label ? ` · ${label}` : ''),
      h('span.dt-dots', this.mediaList.map((x, i) => h('i', { class: (i === this.mediaIndex ? 'on' : '') + (x.type === 'video' ? ' vid' : '') }))),
    );
  }

  showMedia(index, user = false) {
    const n = this.mediaList?.length;
    if (!n || this.playing) return;
    index = (index + n) % n;
    if (index === this.mediaIndex) return;
    this.heroEls[this.mediaIndex].classList.remove('on');
    this.mediaIndex = index;
    const el = this.heroEls[index];
    el.classList.remove('on');
    void el.offsetWidth;
    el.classList.add('on');
    this.syncCounter();
    if (user) {
      this.audio.sfx('move', { pitch: 2 });
      this.restartCarousel();
    }
    if (this.tab === 'shots') this.syncShotsTab();
    this.renderAction();
  }

  /** Screenshots rotate on their own; a selected video stays put until you play it. */
  restartCarousel() {
    clearInterval(this.carousel);
    const shots = this.mediaList?.filter((m) => m.type === 'shot').length || 0;
    if (shots < 2 || this.tab === 'shots') return;
    this.carousel = setInterval(() => {
      if (this.playing || this.mediaList[this.mediaIndex].type === 'video') return;
      let i = this.mediaIndex;
      do i = (i + 1) % this.mediaList.length;
      while (this.mediaList[i].type !== 'shot');
      this.showMedia(i);
    }, 4800);
  }

  // --- video player -----------------------------------------------------------
  // The video plays on the top screen while its overlays fade out. Gameplay clips
  // also play their bottom-screen half on the touch screen, in sync. Tapping the
  // touch screen brings the overlays and controls back for a few seconds.
  startVideo(m) {
    if (this.playing) this.stopVideo(true);
    clearInterval(this.carousel);
    const v = this.videoTop;
    v.src = m.src || m.top;
    v.classList.toggle('native', !m.src);
    const soundOn = this.state.get('music') || this.state.get('sfx');
    v.muted = !m.audio || !soundOn;
    v.volume = Math.min(1, this.state.get('volume'));
    if (m.audio && soundOn) this.audio.duck(true);

    const bottomVideo = m.bottom ? h('video.pl-bottom-video', { src: m.bottom, playsinline: true, preload: 'auto' }) : null;
    if (bottomVideo) bottomVideo.muted = true;
    const ctrl = this.playerControls(m);
    const content = bottomVideo || h('div.pl-panel', h('div.bg-forge'));
    this.playerEl = h('div.player' + (bottomVideo ? '.split' : ''), content, ctrl.el);
    this.playerEl.addEventListener('click', (e) => !e.target.closest('button, .pl-track') && this.toggleVideoUI());
    this.bottom.append(this.playerEl);
    animate(this.playerEl, [{ opacity: 0 }, { opacity: 1 }], { duration: 320, fill: 'none' });

    this.playing = { media: m, bottomVideo, ctrl, video: v };
    const sync = () => {
      const duration = v.duration || m.duration;
      ctrl.set(v.currentTime / duration, v.currentTime, duration, !v.paused);
      if (bottomVideo && Math.abs(bottomVideo.currentTime - v.currentTime) > 0.12) bottomVideo.currentTime = v.currentTime;
    };
    v.ontimeupdate = sync;
    v.onplay = () => {
      bottomVideo?.play().catch(() => {});
      sync();
      this.scheduleHide();
    };
    v.onpause = () => {
      bottomVideo?.pause();
      sync();
    };
    v.onended = () => {
      this.showVideoUI(true);
      sync();
    };
    this.top.classList.add('playing');
    this.showVideoUI(true);
    this.audio.sfx('select');
    v.play().catch(() => {});
  }

  playerControls(m) {
    const play = h('button.pl-btn.pl-main', { type: 'button', 'aria-label': 'Play / Pause', on: { click: () => this.togglePlay() } }, icon('pause'));
    const skip = (seconds) => h('button.pl-btn.pl-skip', { type: 'button', 'aria-label': `${seconds > 0 ? '+' : '-'}5 s`, on: { click: () => this.seekBy(seconds) } }, icon(seconds < 0 ? 'rewind5' : 'forward5'));
    const fill = h('i');
    const knob = h('b');
    const track = h('div.pl-track', { role: 'slider', 'aria-label': 'Video position', 'aria-valuemin': '0', 'aria-valuemax': '100', tabindex: 0 }, fill, knob);
    const seek = (e) => {
      const r = track.getBoundingClientRect();
      this.seekTo((e.clientX - r.left) / r.width);
    };
    track.addEventListener('pointerdown', (e) => { track.setPointerCapture(e.pointerId); seek(e); });
    track.addEventListener('pointermove', (e) => { if (track.hasPointerCapture(e.pointerId)) seek(e); });
    track.addEventListener('pointerup', (e) => { seek(e); track.releasePointerCapture(e.pointerId); });
    const currentTime = h('span', '0:00');
    const durationTime = h('span', fmtTime(m.duration));
    const mute = h('button.pl-btn.pl-mute', { type: 'button', 'aria-label': 'Mute / Unmute', on: { click: () => this.toggleMute() } }, icon(this.videoTop.muted ? 'mute' : 'volume'));
    if (!m.audio) mute.disabled = true;
    const close = h('button.pl-btn.pl-close', { type: 'button', 'aria-label': t('action.back'), on: { click: () => this.stopVideo() } }, icon('back'));
    const el = h('div.pl-controls', close, mute, h('div.pl-transport', skip(-5), play, skip(5)), h('div.pl-timeline', track, h('div.pl-time', currentTime, durationTime)));
    return {
      el,
      set(p, current, duration, playing) {
        const ratio = clamp(p || 0, 0, 1);
        fill.style.width = knob.style.left = `${ratio * 100}%`;
        track.setAttribute('aria-valuenow', String(Math.round(ratio * 100)));
        currentTime.textContent = fmtTime(current);
        durationTime.textContent = fmtTime(duration);
        play.replaceChildren(icon(playing ? 'pause' : 'play'));
      },
      muted(on) {
        mute.replaceChildren(icon(on ? 'mute' : 'volume'));
        mute.classList.toggle('muted', on);
      },
    };
  }

  togglePlay() {
    const v = this.playing?.video;
    if (!v) return;
    if (v.paused || v.ended) {
      if (v.ended) v.currentTime = 0;
      v.play().catch(() => {});
    } else {
      v.pause();
      this.showVideoUI(true);
    }
    this.audio.sfx('toggle');
  }

  seekTo(ratio) {
    const v = this.playing?.video;
    if (!v || !v.duration) return;
    v.currentTime = clamp(ratio, 0, 1) * v.duration;
    this.showVideoUI(true);
  }

  seekBy(seconds) {
    const v = this.playing?.video;
    if (!v || !v.duration) return;
    this.seekTo((v.currentTime + seconds) / v.duration);
    this.audio.sfx('move');
  }

  toggleMute() {
    const p = this.playing;
    if (!p?.media.audio) return;
    p.video.muted = !p.video.muted;
    this.audio.duck(!p.video.muted);
    p.ctrl.muted(p.video.muted);
    this.audio.sfx('toggle');
  }

  /** Shows or hides the top-screen overlays (and the touch controls of split clips). */
  showVideoUI(on) {
    if (!this.playing) return;
    this.videoUI = on;
    this.top.classList.toggle('ui', on);
    this.playerEl.classList.toggle('hide-ui', !on);
    this.app.statusbar.show(on);
    if (on) this.scheduleHide();
  }

  toggleVideoUI() {
    this.showVideoUI(!this.videoUI);
  }

  scheduleHide() {
    clearTimeout(this.hideTimer);
    this.hideTimer = setTimeout(() => {
      const v = this.playing?.video;
      if (v && !v.paused && !v.ended) this.showVideoUI(false);
    }, 3200);
  }

  stopVideo(quiet = false) {
    const p = this.playing;
    if (!p) return;
    this.playing = null;
    clearTimeout(this.hideTimer);
    const v = p.video;
    v.onplay = v.onpause = v.ontimeupdate = v.onended = null;
    v.pause();
    v.removeAttribute('src');
    v.load();
    p.bottomVideo?.pause();
    this.top.classList.remove('playing', 'ui');
    this.audio.duck(false);
    if (this.app.scene === this) this.app.statusbar.show(this.showStatusbar());
    const el = this.playerEl;
    this.playerEl = null;
    if (quiet) el.remove();
    else {
      this.audio.sfx('back');
      animate(el, [{ opacity: 1 }, { opacity: 0 }], { duration: 260 }).then(() => el.remove());
    }
    this.restartCarousel();
  }

  // --- bottom screen ------------------------------------------------------------
  buildBottom() {
    this.backBtn = h('button.dt-back', { type: 'button', 'aria-label': t('action.back'), on: { click: () => this.close() } }, icon('back'));
    this.tabBar = new TabBar(
      TABS.map((id) => ({ value: id, label: t('detail.tab.' + id) })),
      this.tab,
      (v) => this.setTab(v),
    );
    this.tabsSeg = this.tabBar.el;
    this.disposers.push(() => this.tabBar.destroy());
    this.content = h('div.scroll-content');
    this.scrollEl = h('div.dt-scroll.scroller', this.content);
    this.scroller = new Scroller(this.scrollEl, this.content);
    this.action = h('div.dt-action');
    this.bottom.append(h('div.bg-forge'), h('div.dt-bottom', h('div.dt-bar', this.backBtn, this.tabsSeg), this.scrollEl, this.action));
    this.renderTab();
    this.renderAction();
  }

  setTab(id) {
    if (id === this.tab) return;
    const dir = Math.sign(TABS.indexOf(id) - TABS.indexOf(this.tab));
    this.tab = id;
    this.tabsSeg.seg.set(id);
    this.audio.sfx('tab');
    this.renderTab();
    this.renderAction();
    revealTabContent(this.content.firstChild, dir);
    this.app.syncChrome();
    this.restartCarousel();
  }

  renderTab(keepScroll = false) {
    this.top.classList.toggle('gallery', this.tab === 'shots');
    const y = this.scroller.y;
    const build = { info: () => this.infoTab(), shots: () => this.shotsTab(), install: () => this.installTab(), news: () => this.newsTab() }[this.tab];
    this.content.replaceChildren(build());
    if (keepScroll) this.scroller.scrollTo(y, false);
    else this.scroller.reset();
    requestAnimationFrame(() => this.scroller.syncBar());
  }

  // Info ----------------------------------------------------------------------
  infoTab() {
    const item = this.item;
    const rel = this.catalog.release(item, this.channel);
    const parts = [];
    if (item.status === 'development') {
      parts.push(
        h(
          'div.dev-card',
          h('div.dev-head', icon('hammer'), h('b', t('detail.devTitle'))),
          h('p', t('detail.devBody')),
          h('div.dev-progress', h('span', t('detail.progress')), h('b', `${item.progress} %`)),
          h('div.bar.static', h('i', { style: { '--p': item.progress / 100, background: 'linear-gradient(90deg, var(--lavender), #b9a8ff)' } })),
        ),
      );
    }
    const tagline = L(item.tagline);
    const description = L(item.description);
    if (tagline && tagline !== description) parts.push(h('p.dt-tagline', tagline));
    parts.push(h('p.dt-desc', description));
    const features = L(item.features);
    if (features.length) parts.push(h('div.eyebrow.dt-eyebrow', t('detail.features')), h('ul.dt-features', features.map((f) => h('li', icon('check'), h('span', f)))));
    if (item.source === 'universal') parts.push(h('p.dt-source', icon('globe'), t('udb.source')));
    const compat = item.compat || {};
    const consoleText = compat.o3ds === false ? t('detail.consoleNew') : compat.o3ds === 'experimental' ? t('detail.consoleNewExp') : t('detail.consoleBoth');
    const stereo = compat.stereo === 'experimental' ? t('detail.experimental') : compat.stereo ? t('detail.yes') : t('detail.no');
    const facts = [
      [t('detail.version'), rel ? `${rel.tag} · ${t('channel.' + rel.channel)}` : '—'],
      [t('detail.size'), rel ? fmtSize(this.catalog.downloadSize(item, this.format, rel)) : '—'],
      [t('detail.author'), item.author],
      [t('detail.original'), item.origin ? `${item.origin.platform}${item.origin.year ? ' · ' + item.origin.year : ''}` : '—'],
      [t('detail.console'), consoleText],
      [t('detail.stereo'), stereo],
      [t('detail.updated'), rel ? fmtDate(rel.date) : '—'],
      [t('detail.stars'), item.stars != null ? `★ ${fmtStars(item.stars)}` : '—'],
    ];
    if (item.license) facts.push([t('detail.license'), item.license]);
    const wide = [];
    if (item.repo) wide.push([t('detail.repo'), h('a.dt-link', { href: `https://github.com/${item.repo}`, target: '_blank', rel: 'noopener' }, icon('github'), item.repo)]);
    if (item.credits) wide.push([t('detail.basedOn'), item.credits]);
    parts.push(
      h('div.eyebrow.dt-eyebrow', t('detail.facts')),
      h(
        'div.dt-facts',
        facts.map(([k, v]) => h('div.fact', h('span.k', k), h('span.v', v))),
        wide.map(([k, v]) => h('div.fact.wide', h('span.k', k), h('span.v', v))),
      ),
    );
    return h('div.dt-pad', parts);
  }

  // Screens -------------------------------------------------------------------
  shotsTab() {
    if (!this.mediaList.length) return h('div.dt-empty', icon('eye'), t('detail.noShots'));
    this.thumbs = this.mediaList.map((m, i) =>
      h(
        'button.dt-thumb' + (m.type === 'video' ? '.video' : ''),
        { type: 'button', on: { click: () => this.showMedia(i, true) } },
        h('img', { src: m.type === 'video' ? m.poster : m.top, alt: '' }),
        m.type === 'video' ? h('span.dt-thumb-play', icon('play')) : null,
      ),
    );
    this.thumbTrack = h('div.dt-thumbs-track', this.thumbs);
    this.shotView = h('div.dt-shot-view');
    const el = h('div.dt-pad.dt-shots', h('div.dt-thumbs', this.thumbTrack), this.shotView);
    requestAnimationFrame(() => this.syncShotsTab());
    return el;
  }

  syncShotsTab() {
    if (!this.thumbs || !this.shotView) return;
    const m = this.mediaList[this.mediaIndex];
    this.thumbs.forEach((b, i) => b.classList.toggle('on', i === this.mediaIndex));
    const selected = this.thumbs[this.mediaIndex];
    const view = this.thumbTrack.parentElement.clientWidth;
    const shift = clamp(selected.offsetLeft + selected.offsetWidth / 2 - view / 2, 0, Math.max(0, this.thumbTrack.scrollWidth - view));
    this.thumbTrack.style.transform = `translateX(${-shift}px)`;
    if (m.type === 'video') {
      const preview = m.bottom
        ? h('video', { src: m.bottom, muted: true, preload: 'auto', playsinline: true, on: { loadedmetadata: (e) => (e.target.currentTime = 0.5) } })
        : h('img', { src: m.poster, alt: '' });
      this.shotView.replaceChildren(
        h('div.dt-shot-bottom' + (m.bottom ? '' : '.wide'), preview, h('button.dt-shot-play', { type: 'button', on: { click: () => this.startVideo(m) } }, icon('play'))),
        h(
          'div.dt-shot-side',
          h('div.dt-media-heading', h('div.dt-shot-caption', L(m.title)), h('span.dt-media-duration', fmtTime(m.duration))),
          h('button.ui-btn.primary.dt-playbtn', { type: 'button', on: { click: () => this.startVideo(m) } }, icon('play'), h('span', t('video.play'))),
        ),
      );
    } else {
      this.shotView.replaceChildren(
        h('div.dt-shot-bottom', m.bottom ? h('img', { src: m.bottom, alt: '' }) : h('div.dt-shot-none', icon('eye'))),
        h('div.dt-shot-side', h('div.dt-shot-caption', L(m.caption)), h('p.faint', t('detail.shotHint'))),
      );
    }
    this.shotView.querySelector('.dt-shot-side').append(h('div.dt-media-nav',
      h('button.ui-btn.surface', { type: 'button', 'aria-label': t('detail.previousMedia'), on: { click: () => this.showMedia((this.mediaIndex + this.mediaList.length - 1) % this.mediaList.length, true) } }, icon('back')),
      h('span', `${this.mediaIndex + 1} / ${this.mediaList.length}`),
      h('button.ui-btn.surface', { type: 'button', 'aria-label': t('detail.nextMedia'), on: { click: () => this.showMedia((this.mediaIndex + 1) % this.mediaList.length, true) } }, icon('next'))));
    animate(this.shotView.firstChild, [{ opacity: 0, transform: 'scale(.96)' }, { opacity: 1, transform: 'none' }], { duration: 340, fill: 'none' });
  }

  // Install -------------------------------------------------------------------
  /** Everything the script will leave on the SD card for a format/channel. */
  plan(format = this.format, channel = this.channel) {
    const item = this.item;
    const rel = this.catalog.release(item, channel);
    const tmp = this.catalog.resolve('%TMP%');
    const dsp = this.catalog.requirement(item, 'dspfirm');
    const entries = new Map();
    let cia = false;
    for (const step of item.install?.[format] || []) {
      if (step.type === 'mkdir') entries.set(normalize(this.catalog.resolve(step.directory)), { type: 'dir' });
      if (step.type === 'downloadRelease' || step.type === 'downloadUrl') {
        const out = this.catalog.resolve(step.output);
        const size = step.type === 'downloadUrl' ? step.size : this.catalog.asset(item, rel, step.asset).size;
        // Files the script deletes afterwards (temporary downloads) are not shown.
        const deleted = (item.install[format] || []).some((s) => s.type === 'deleteFile' && this.catalog.resolve(s.file) === out);
        if (!out.startsWith(tmp) && !deleted) entries.set(out, { type: 'file', size, github: !!item.repo });
      }
      if (step.type === 'move') {
        const from = this.catalog.resolve(step.old);
        const to = this.catalog.resolve(step.new);
        const moved = entries.get(from);
        entries.delete(from);
        entries.set(to, moved || { type: 'file', size: 0 });
      }
      if (step.type === 'extractFile') {
        for (const c of step.creates || []) {
          const p = this.catalog.resolve(c.path);
          if (!p.startsWith(tmp)) entries.set(p, { type: 'file', size: c.size });
        }
      }
      if (step.type === 'installCia') cia = true;
      if (step.type === 'dumpDsp' && dsp) entries.set(dsp.path, { type: 'file', size: dsp.size, dsp: true });
    }
    // Where the player's own files go (the ROM), shown right inside the tree.
    for (const req of item.requires || []) {
      const file = this.catalog.requirementFile(req);
      if (file) entries.set(file, { type: 'file', req, size: req.size });
    }
    for (const p of [...entries.keys()]) {
      let parent = parentOf(p);
      while (parent !== '/') {
        if (!entries.has(parent)) entries.set(parent, { type: 'dir' });
        parent = parentOf(parent);
      }
    }
    return { entries, cia, rel };
  }

  installTab() {
    const item = this.item;
    const parts = [];
    if (!item.install) {
      parts.push(h('div.dev-card', h('div.dev-head', icon('hammer'), h('b', t('detail.devTitle'))), h('p', t('detail.devBody'))));
    } else {
      const formats = this.catalog.formats(item);
      const channels = this.catalog.channels(item);
      const options = h('div.dt-options');
      if (formats.length > 1) {
        options.append(
          h('div.opt-row', h('span.k', t('confirm.format')), segmented(formats.map((f) => ({ value: f, label: `${t('format.' + f)} · ${t('format.' + f + 'Hint')}` })), this.format, (v) => {
            this.format = v;
            this.audio.sfx('toggle');
            this.renderTab(true);
            this.renderAction();
          })),
        );
      }
      if (channels.length > 1) {
        options.append(
          h('div.opt-row', h('span.k', t('confirm.channel')), segmented(channels.map((c) => ({ value: c, label: `${t('channel.' + c)} ${item.releases[c].tag}` })), this.channel, (v) => {
            this.channel = v;
            this.audio.sfx('toggle');
            this.renderTab(true);
            this.renderAction();
            this.renderChips();
          })),
        );
      }
      if (options.childElementCount) parts.push(options);
      if (item.notice) parts.push(h('div.notice', h('b', icon('info'), t('detail.notice')), h('p', L(item.notice))));
      parts.push(h('div.eyebrow.dt-eyebrow', t('detail.sdTitle')), this.tree());
    }
    parts.push(h('div.eyebrow.dt-eyebrow', t('detail.youProvide')));
    const reqs = item.requires || [];
    if (!reqs.length) parts.push(h('p.dt-desc', icon('check'), ' ', t('detail.noRequirements')));
    reqs.forEach((req) => parts.push(this.reqCard(req)));
    if (item.install?.[this.format]?.some((s) => s.type === 'dumpDsp')) parts.push(h('p.dt-note', icon('volume'), h('span', t('detail.dspNote'))));
    return h('div.dt-pad', parts);
  }

  /** The SD card as it will look: folders first at every level, then files. */
  tree() {
    const { entries, cia } = this.plan();
    const root = { children: new Map() };
    for (const [path, entry] of entries) {
      let node = root;
      for (const part of splitPath(path)) {
        if (!node.children.has(part)) node.children.set(part, { children: new Map() });
        node = node.children.get(part);
      }
      Object.assign(node, { path, entry });
    }
    const rows = [h('div.tree-row.root', icon('sd'), h('span', 'sdmc:/'))];
    const walk = (node, depth) => {
      const kids = [...node.children.values()].sort((a, b) => {
        const da = a.entry.type === 'dir';
        const db = b.entry.type === 'dir';
        return da !== db ? (da ? -1 : 1) : baseName(a.path).localeCompare(baseName(b.path));
      });
      for (const kid of kids) {
        rows.push(this.treeRow(kid.path, kid.entry, depth));
        walk(kid, depth + 1);
      }
    };
    walk(root, 1);
    if (cia) rows.push(h('div.tree-row.cia', icon('home'), h('span.name', t('detail.homeMenu')), h('span.tree-note', this.item.title)));
    return h('div.tree.card', rows);
  }

  treeRow(path, e, depth) {
    const exists = this.app.sd.exists(path);
    let cls = e.type === 'dir' ? '.dir' : '.file';
    let ic = e.type === 'dir' ? (exists ? 'folder' : 'folderPlus') : 'file';
    let note;
    if (e.type === 'dir') {
      note = h('span.tree-note' + (exists ? '.faint' : '.new'), exists ? t('detail.exists') : t('detail.newFolder'));
    } else if (e.req) {
      cls += '.rom' + (exists ? '.ok' : '');
      ic = 'rom';
      note = h('span.chip' + (exists ? '.mint' : '.amber'), icon(exists ? 'check' : 'warn'), exists ? t('detail.found') : t('detail.romHere'));
    } else if (e.dsp) {
      cls += '.dsp';
      ic = 'volume';
      note = h('span.tree-note' + (exists ? '.faint' : '.new'), exists ? t('detail.exists') : t('detail.dspAuto'));
    } else {
      note = h('span.tree-note', fmtSize(e.size), e.github ? icon('github') : null);
    }
    const fresh = !exists && !e.req ? '.fresh' : '';
    return h('div.tree-row' + cls + fresh, { style: { '--depth': depth } }, icon(ic), h('span.name.ellipsis', baseName(path) + (e.type === 'dir' ? '/' : '')), note);
  }

  reqCard(req) {
    const item = this.item;
    const has = this.catalog.hasRequirement(item, req);
    const file = this.catalog.requirementFile(req);
    return h(
      'div.req-card' + (has ? '.ok' : '.missing'),
      h(
        'div.req-head',
        h('span.req-icon', icon(req.id === 'dspfirm' ? 'volume' : req.id === 'rom' ? 'rom' : 'file')),
        h('div.req-title', h('b', L(req.label)), file ? h('code', 'sdmc:' + file) : h('small', t('detail.repoReadme'))),
        has ? h('span.chip.mint', icon('check'), t('detail.found')) : h('span.chip.amber', icon('warn'), t('detail.missing')),
      ),
      req.accept ? h('p.req-line', t('detail.anyName', { names: req.accept })) : null,
      req.hashes ? h('p.req-line', t('detail.hashes', { list: req.hashes.map((x) => x.label).join(', ') })) : null,
      req.help ? h('p.req-help', L(req.help)) : null,
      h(
        'div.req-actions',
        !has ? h('button.ui-btn.small', { type: 'button', on: { click: () => this.simulateCopy(req) } }, icon('sd'), t('detail.simulateCopy')) : null,
        req.tool ? h('a.ui-btn.small.ghost', { href: req.tool.url, target: '_blank', rel: 'noopener' }, icon('link'), L(req.tool.label)) : null,
      ),
    );
  }

  simulateCopy(req) {
    const file = this.catalog.requirementFile(req);
    if (file) this.app.sd.writeFile(file, req.size || 1048576, { kind: 'user' });
    else this.state.satisfied[`${this.item.id}:${req.id}`] = true;
    this.state.save(this.app.sd);
    this.audio.sfx('toggle');
    this.app.toast({ text: t('toast.copied', { label: L(req.label) }), icon: 'sd' });
    this.refresh();
  }

  // What's new ------------------------------------------------------------------
  /** Every release, newest first: current stable / experimental plus the history. */
  releaseList() {
    const item = this.item;
    const list = [];
    for (const channel of ['experimental', 'stable']) {
      const rel = item.releases?.[channel];
      if (rel) list.push({ ...rel, channel });
    }
    list.push(...(item.history || []));
    const seen = new Set();
    return list.filter((r) => !seen.has(r.tag) && seen.add(r.tag)).sort((a, b) => b.date.localeCompare(a.date) || (a.channel === 'stable' ? 1 : -1));
  }

  newsTab() {
    const list = this.releaseList();
    if (!list.length) return h('div.dt-pad', h('div.dev-card', h('div.dev-head', icon('hammer'), h('b', t('detail.devTitle'))), h('p', t('detail.devBody'))));
    this.newsOpen = clamp(this.newsOpen ?? 0, 0, list.length - 1);
    this.newsCards = list.map((rel, i) =>
      h(
        'div.news-card' + (i === this.newsOpen ? '.open' : ''),
        h(
          'button.news-head',
          { type: 'button', on: { click: () => this.openNews(i) } },
          h('b', rel.tag),
          h('span.chip' + (rel.channel === 'experimental' ? '.lav' : '.mint'), t('channel.' + rel.channel)),
          i === 0 ? h('span.chip.forge', t('detail.latest')) : null,
          h('span.news-date', fmtDate(rel.date)),
          icon('next', 'chev'),
        ),
        h('div.news-body', h('ul', L(rel.notes).map((n) => h('li', n)))),
      ),
    );
    return h('div.dt-pad', h('div.eyebrow.dt-eyebrow', t('detail.history', { n: list.length })), h('div.news-list', this.newsCards));
  }

  /** Accordion: the tapped release expands while the open one folds away. */
  openNews(index) {
    if (!this.newsCards || index < 0 || index >= this.newsCards.length || index === this.newsOpen) return;
    this.newsCards[this.newsOpen]?.classList.remove('open');
    this.newsOpen = index;
    const card = this.newsCards[index];
    card.classList.add('open');
    this.audio.sfx('tab');
    setTimeout(() => {
      this.scroller.syncBar();
      this.scroller.reveal(card, 6);
    }, 380);
  }

  // --- action bar ---------------------------------------------------------------
  /** In the Screens tab with a video selected, A plays it instead of the main action. */
  get aPlays() {
    return this.tab === 'shots' && this.mediaList?.[this.mediaIndex]?.type === 'video';
  }

  renderAction() {
    const item = this.item;
    const status = this.catalog.status(item, this.installer.jobs);
    const aPlays = this.aPlays;
    const btn = (variant, ic, label, g, fn, disabled) =>
      h('button.ui-btn.dt-primary' + (variant ? '.' + variant : ''), { type: 'button', disabled, on: { click: fn } }, icon(ic), h('span.ellipsis', label), g === 'A' && ['available', 'update'].includes(status.kind) && !aPlays ? glyph('A') : null);
    const round = (ic, g, fn, title) => h('button.ui-btn.dt-round', { type: 'button', title, on: { click: fn } }, icon(ic), ic === 'github' && g === 'X' ? glyph('X') : null);
    const parts = [];
    switch (status.kind) {
      case 'available': {
        const size = fmtSize(this.catalog.downloadSize(item, this.format, this.catalog.release(item, this.channel)));
        parts.push(btn('primary', 'download', `${t('action.download')} · ${size}`, 'A', () => this.openSheet()));
        break;
      }
      case 'update':
        parts.push(btn('primary', 'update', t('action.update', { v: status.latest.tag }), 'A', () => this.openSheet()));
        parts.push(round('trash', 'Y', () => this.uninstall(), t('action.uninstall')));
        break;
      case 'installed':
        parts.push(btn('mint', 'play', t('action.open'), 'A', () => this.launch()));
        parts.push(round('trash', 'Y', () => this.uninstall(), t('action.uninstall')));
        break;
      case 'needs-file':
        parts.push(btn('mint', 'play', t('action.open'), 'A', () => this.primary()));
        parts.push(round('trash', 'Y', () => this.uninstall(), t('action.uninstall')));
        break;
      case 'downloading':
      case 'queued': {
        const p = status.job.progress || 0;
        parts.push(
          h(
            'button.dt-progress',
            { type: 'button', on: { click: () => this.app.open('install', { job: status.job }) } },
            h('div.dt-progress-top', h('span', status.kind === 'queued' ? t('status.queued') : t('status.downloading')), h('b', `${Math.round(p * 100)} %`)),
            h('div.bar', h('i', { style: { '--p': p } })),
          ),
        );
        parts.push(round('close', 'X', () => this.installer.cancel(status.job), t('action.cancel')));
        break;
      }
      case 'dev':
        parts.push(btn('ghost', 'hammer', t('action.inDev', { p: item.progress }), null, null, true));
        break;
      default:
    }
    const busy = status.kind === 'downloading' || status.kind === 'queued';
    if (item.repo) parts.push(round('github', busy ? null : 'X', () => this.openGitHub(), t('action.github')));
    this.action.replaceChildren(...parts);
    this.status = status;
  }

  openGitHub() {
    if (!this.item.repo) return;
    this.audio.sfx('open');
    this.app.open('github', { repo: this.item.repo });
  }

  primary() {
    if (this.aPlays) return this.startVideo(this.mediaList[this.mediaIndex]);
    const kind = this.status?.kind;
    if (kind === 'available' || kind === 'update') return this.openSheet();
    if (kind === 'installed') return this.launch();
    if (kind === 'needs-file') {
      this.app.toast({ text: t('toast.needsFile'), icon: 'warn', tone: 'amber' });
      this.audio.sfx('warn');
      return this.setTab('install');
    }
    if (kind === 'downloading' || kind === 'queued') return this.app.open('install', { job: this.status.job });
    this.audio.sfx('error');
  }

  async launch() {
    this.audio.sfx('launch');
    this.app.toast({ text: t('toast.opening', { title: this.item.title }), image: appIcon(this.item, { size: 18 }) });
    const flash = (screen) => {
      const el = h('div.launch-flash');
      screen.append(el);
      animate(el, [{ opacity: 0 }, { opacity: 1, offset: 0.3 }, { opacity: 1, offset: 0.6 }, { opacity: 0 }], { duration: 1400, easing: 'ease-in-out' }).then(() => el.remove());
    };
    flash(this.top);
    flash(this.bottom);
  }

  uninstall() {
    this.installer.uninstall(this.item);
    this.audio.sfx('back');
    this.app.toast({ text: t('toast.uninstalled', { title: this.item.title }), icon: 'trash', tone: 'danger' });
  }

  // --- install confirmation sheet ---------------------------------------------------
  openSheet() {
    if (this.sheet) return;
    const item = this.item;
    this.audio.sfx('open');
    const formats = this.catalog.formats(item);
    const channels = this.catalog.channels(item);
    const sheet = { format: this.format, channel: this.channel, focus: 0, controls: [] };
    this.sheet = sheet;

    const scrim = h('div.scrim', { on: { click: () => this.closeSheet() } });
    const body = h('div.sheet-body');
    const confirm = h('button.ui-btn.primary.grow', { type: 'button', on: { click: () => this.confirm() } }, icon('download'), h('span', t('action.install')), glyph('A'));
    const panel = h(
      'div.sheet',
      h('div.grabber'),
      h('div.sheet-title', appIcon(item, { size: 30 }), h('div', h('h3', t('confirm.title', { title: item.title })), h('small.faint', 'sdmc:' + (item.folder || '/3ds/')))),
      body,
      h('div.sheet-buttons', h('button.ui-btn.ghost', { type: 'button', on: { click: () => this.closeSheet() } }, h('span', t('action.cancel')), glyph('B')), confirm),
    );

    const setChannel = (c) => {
      if (c === sheet.channel) return;
      sheet.channel = c;
      this.audio.sfx('toggle');
      render();
    };
    const render = () => {
      const rel = this.catalog.release(item, sheet.channel);
      const compat = this.catalog.compat(item, rel);
      const size = this.catalog.downloadSize(item, sheet.format, rel);
      const rows = [];
      sheet.controls = [];
      if (channels.length > 1) {
        // Stable or the newest test build, chosen right when installing.
        const cards = channels.map((c) =>
          h(
            'button.chan-card' + (c === sheet.channel ? '.on' : ''),
            { type: 'button', on: { click: () => setChannel(c) } },
            h('i.radio'),
            h('span.chan-text', h('b', `${t('channel.' + c)} · ${item.releases[c].tag}`), h('small', t(c === 'stable' ? 'confirm.stableHint' : 'confirm.expHint'))),
            h('span.chan-date', fmtDate(item.releases[c].date)),
          ),
        );
        const group = h('div.chan-cards', cards);
        sheet.controls.push({ el: group, step: (dir) => setChannel(channels[clamp(channels.indexOf(sheet.channel) + dir, 0, channels.length - 1)]) });
        rows.push(group);
      }
      if (formats.length > 1) {
        const seg = segmented(formats.map((f) => ({ value: f, label: t('format.' + f) })), sheet.format, (v) => {
          sheet.format = v;
          this.audio.sfx('toggle');
          render();
        });
        sheet.controls.push({ el: seg, step: (dir) => seg.seg.step(dir) });
        rows.push(h('div.sheet-row', h('span.k', t('confirm.format')), seg));
      }
      const model = this.state.get('console') === 'o3ds' ? 'Old 3DS' : 'New 3DS XL';
      rows.push(
        h(
          'div.sheet-info',
          h('span' + (compat.ok ? '.ok' : '.bad'), icon(compat.ok ? 'check' : 'warn'), model),
          h('span', icon('download'), fmtSize(size)),
          h('span', icon('sd'), t('confirm.free', { free: fmtSize(this.app.sd.free()) })),
        ),
      );
      if (!compat.ok) rows.push(h('div.sheet-warn.danger', icon('warn'), t('confirm.blockedNew')));
      else if (compat.warn) rows.push(h('div.sheet-warn', icon('warn'), t('confirm.expOld')));
      body.replaceChildren(...rows);
      confirm.disabled = !compat.ok;
      sheet.focus = clamp(sheet.focus, 0, Math.max(0, sheet.controls.length - 1));
      sheet.controls.forEach((c, i) => c.el.classList.toggle('focus', i === sheet.focus));
    };
    render();
    sheet.el = h('div.sheet-wrap', scrim, panel);
    this.bottom.append(sheet.el);
    animate(scrim, [{ opacity: 0 }, { opacity: 1 }], 300);
    animate(panel, [{ transform: 'translateY(110%)' }, { transform: 'none' }], { duration: 420, easing: OUT });
  }

  async closeSheet() {
    const sheet = this.sheet;
    if (!sheet) return;
    this.sheet = null;
    this.audio.sfx('back');
    const [scrim, panel] = sheet.el.children;
    animate(scrim, [{ opacity: 1 }, { opacity: 0 }], 260);
    await animate(panel, [{ transform: 'none' }, { transform: 'translateY(110%)' }], { duration: 280, easing: 'cubic-bezier(0.55, 0, 0.75, 0.2)' });
    sheet.el.remove();
  }

  confirm() {
    const sheet = this.sheet;
    if (!sheet) return;
    const rel = this.catalog.release(this.item, sheet.channel);
    if (!this.catalog.compat(this.item, rel).ok) {
      this.audio.sfx('error');
      return;
    }
    this.format = sheet.format;
    this.channel = sheet.channel;
    const job = this.installer.enqueue(this.item, { format: sheet.format, channel: sheet.channel });
    this.sheet = null;
    sheet.el.remove();
    this.audio.sfx('download');
    this.renderAction();
    this.app.open('install', { job });
  }

  sheetButton(btn) {
    const sheet = this.sheet;
    if (btn === 'B') return this.closeSheet();
    if (btn === 'A') return this.confirm();
    if (!sheet.controls.length) return;
    if (btn === 'UP' || btn === 'DOWN') {
      sheet.focus = clamp(sheet.focus + (btn === 'DOWN' ? 1 : -1), 0, sheet.controls.length - 1);
      sheet.controls.forEach((c, i) => c.el.classList.toggle('focus', i === sheet.focus));
      this.audio.sfx('move');
    }
    if (btn === 'LEFT' || btn === 'RIGHT' || btn === 'L' || btn === 'R') sheet.controls[sheet.focus]?.step(btn === 'RIGHT' || btn === 'R' ? 1 : -1);
  }

  // --- input ------------------------------------------------------------------
  onButton(btn) {
    if (this.sheet) return this.sheetButton(btn);
    if (this.playing) return this.playerButton(btn);
    switch (btn) {
      case 'B':
        return this.close();
      case 'A':
        return this.primary();
      case 'L':
      case 'R': {
        const i = TABS.indexOf(this.tab) + (btn === 'R' ? 1 : -1);
        if (i >= 0 && i < TABS.length) this.setTab(TABS[i]);
        else this.audio.sfx('move', { pitch: -5 });
        return;
      }
      case 'LEFT':
      case 'RIGHT':
        return this.showMedia(this.mediaIndex + (btn === 'RIGHT' ? 1 : -1), true);
      case 'UP':
      case 'DOWN':
        if (this.tab === 'news') return this.openNews(this.newsOpen + (btn === 'DOWN' ? 1 : -1));
        return this.scroller.by(btn === 'DOWN' ? 34 : -34);
      case 'X':
        if (this.status?.job) this.installer.cancel(this.status.job);
        else this.openGitHub();
        return;
      case 'Y':
        if (['installed', 'needs-file', 'update'].includes(this.status?.kind)) this.uninstall();
        return;
      default:
    }
  }

  playerButton(btn) {
    if (btn === 'B') return this.stopVideo();
    if (btn === 'A' || btn === 'START') return this.togglePlay();
    if (btn === 'LEFT' || btn === 'RIGHT') return this.seekBy(btn === 'RIGHT' ? 5 : -5);
    if (btn === 'Y') return this.toggleMute();
    this.showVideoUI(true);
  }

  close() {
    if (this.closing) return;
    this.closing = true;
    this.audio.sfx('back');
    this.app.back('zoom').then(() => (this.closing = false));
  }

  onLanguage() {
    this.tabBar.setOptions(TABS.map((id) => ({ value: id, label: t('detail.tab.' + id) })));
    this.tabsSeg.seg.sync(false);
    this.renderChips();
    this.syncCounter();
    this.renderTab(true);
    this.renderAction();
  }

  onShow() {
    this.audio.muffle(0.35);
    this.restartCarousel();
    this.banner?.resume();
  }

  onHide() {
    clearInterval(this.carousel);
    this.stopVideo(true);
  }

  destroy() {
    super.destroy();
    clearInterval(this.carousel);
    this.stopVideo(true);
    this.banner?.destroy();
  }

  // --- transitions --------------------------------------------------------------
  enter(how, prev) {
    if (how !== 'zoom') return super.enter(how, prev);
    const flights = [];
    const from = prev?.cardIcon?.getBoundingClientRect();
    if (from) {
      const to = this.headIcon.getBoundingClientRect();
      const scale = to.width / this.headIcon.offsetWidth || 1;
      const dx = (from.left - to.left) / scale;
      const dy = (from.top - to.top) / scale;
      const s = from.width / to.width;
      flights.push(
        animate(this.headIcon, [{ transform: `translate(${dx}px, ${dy}px) scale(${s})` }, { transform: 'none' }], { duration: 560, easing: 'cubic-bezier(0.34, 1.2, 0.64, 1)', fill: 'backwards' }),
      );
    }
    animate(this.head.querySelector('.dt-head-text'), [{ opacity: 0, transform: 'translateX(-10px)' }, { opacity: 1, transform: 'none' }], { duration: 480, delay: 140, fill: 'backwards' });
    return Promise.all([
      animate(this.hero, [{ opacity: 0, transform: 'scale(1.08)' }, { opacity: 1, transform: 'none' }], { duration: 560, easing: OUT, fill: 'backwards' }),
      animate(this.bottom, [{ opacity: 0, transform: 'translateY(26px)' }, { opacity: 1, transform: 'none' }], { duration: 480, easing: OUT }),
      ...flights,
    ]);
  }

  leave(how, next) {
    if (how !== 'zoom') return super.leave(how, next);
    return Promise.all([
      animate(this.top, [{ opacity: 1, transform: 'none' }, { opacity: 0, transform: 'scale(.97)' }], { duration: 300, easing: 'ease-in' }),
      animate(this.bottom, [{ opacity: 1, transform: 'none' }, { opacity: 0, transform: 'translateY(26px)' }], { duration: 300, easing: 'ease-in' }),
    ]);
  }

}
