// Download queue: the running install on top with the SD space, and every job
// (running, queued and finished) on the touch screen.
import { Scene } from './scene.js';
import { h, clamp } from '../core/dom.js';
import { t, fmtSize, fmtEta } from '../core/i18n.js';
import { icon } from '../core/icons.js';
import { appIcon, glyph, Scroller, sdMeter } from '../ui/components.js';
import { coin } from '../ui/coin.js';

const RING = 2 * Math.PI * 9;

export class DownloadsScene extends Scene {
  static section = 'downloads';

  build() {
    this.focus = 0;
    this.activeEl = h('div');
    this.meterEl = h('div');
    this.sub = h('div.pn-sub');
    this.top.append(h('div.bg-forge'), h('div.pn-top', h('h2.pn-title', h('b', t('downloads.title'))), this.sub, this.activeEl), this.meterEl);

    this.list = h('div.scroll-content.pn-list');
    this.scrollEl = h('div.pn-scroll.scroller', this.list);
    this.scroller = new Scroller(this.scrollEl, this.list);
    this.note = h('span.pn-bar-note');
    this.bottom.append(
      h('div.bg-forge'),
      h('div.pn-bar', h('button.dt-back', { type: 'button', on: { click: () => this.leaveScene() } }, icon('back'), glyph('B')), h('div.pn-bar-title', t('downloads.title')), this.note),
      this.scrollEl,
    );
    this.render();
    this.listen(this.installer, 'progress', () => this.tick());
    this.listen(this.installer, 'step', () => this.tick());
    this.listen(this.installer, 'change', () => this.render());
    this.listen(this.installer, 'uninstall', () => this.render());
  }

  /** Running first, then the queue in order, then finished jobs newest first. */
  jobs() {
    const all = this.installer.jobs;
    const rank = { running: 0, queued: 1 };
    return [...all].sort((a, b) => (rank[a.status] ?? 2) - (rank[b.status] ?? 2) || (rank[a.status] != null ? a.id - b.id : b.id - a.id));
  }

  render() {
    this.items = this.jobs();
    this.focus = clamp(this.focus, 0, Math.max(0, this.items.length - 1));
    const active = this.installer.active();
    this.sub.textContent = active.length ? t('downloads.ofQueue', { n: active.length }) : t('downloads.emptyBody');
    this.note.textContent = this.items.length ? `${this.items.length}` : '';
    if (!this.items.length) {
      this.rowEls = [];
      this.list.replaceChildren(h('div.pn-empty', icon('download'), t('downloads.empty'), h('small.faint', t('downloads.emptyBody'))));
    } else {
      this.rowEls = this.items.map((job, i) => this.row(job, i));
      const parts = [];
      let section = null;
      this.items.forEach((job, i) => {
        const key = job.status === 'running' ? 'downloads.active' : job.status === 'queued' ? 'downloads.queue' : 'downloads.history';
        if (key !== section) parts.push(h('div.pn-section', t(key)));
        section = key;
        parts.push(this.rowEls[i]);
      });
      this.list.replaceChildren(...parts);
    }
    requestAnimationFrame(() => this.scroller.syncBar());
    this.renderTop();
  }

  row(job, i) {
    const right = [];
    if (job.status === 'running') {
      right.push(h('span.job-ring', { html: `<svg viewBox="0 0 22 22"><circle cx="11" cy="11" r="9"/><circle class="p" cx="11" cy="11" r="9" style="stroke-dashoffset:${RING * (1 - job.progress)}"/></svg>` }));
    } else if (job.status === 'queued') right.push(h('span.chip', icon('clock'), t('status.queued')));
    else if (job.status === 'done') right.push(job.missing.length ? h('span.chip.amber', icon('warn'), t('status.needsFile')) : h('span.chip.mint', icon('check'), t('status.installed')));
    else right.push(h('span.chip.danger', icon('close'), t(job.status === 'cancelled' ? 'install.cancelled' : 'install.failed')));
    return h(
      'button.li' + (i === this.focus ? '.focus' : ''),
      { type: 'button', on: { click: () => (i === this.focus ? this.open() : this.setFocus(i)) } },
      appIcon(job.app, { size: 22 }),
      h('span.li-text', h('b', job.app.title), h('small', this.statusLine(job))),
      ...right,
    );
  }

  statusLine(job) {
    if (job.status === 'running') {
      const step = job.steps.find((s) => s.status === 'active');
      return `${Math.round(job.progress * 100)} % · ${step ? t(step.key, step.vars) : ''}`;
    }
    if (job.status === 'queued') return `${job.tag} · ${job.format.toUpperCase()} · ${fmtSize(job.bytesTotal)}`;
    return `${job.tag} · ${job.format.toUpperCase()} · ${t('channel.' + job.channel)}`;
  }

  /** Cheap update while bytes flow: only the running row and the top card. */
  tick() {
    const i = this.items.findIndex((j) => j.status === 'running');
    if (i < 0) return;
    const job = this.items[i];
    const row = this.rowEls[i];
    row.querySelector('small').textContent = this.statusLine(job);
    const ring = row.querySelector('circle.p');
    if (ring) ring.style.strokeDashoffset = String(RING * (1 - job.progress));
    this.renderTop();
  }

  renderTop() {
    const job = this.installer.running?.status === 'running' ? this.installer.running : null;
    if (job) {
      const step = job.steps.find((s) => s.status === 'active');
      this.activeEl.replaceChildren(
        h(
          'div.dl-active.pn-card',
          appIcon(job.app, { size: 44 }),
          h(
            'div.grow',
            h('b.ellipsis', job.app.title),
            h('small.ellipsis', step ? t(step.key, step.vars) : ''),
            h('div.bar', h('i', { style: { '--p': job.progress } })),
            h('div.meta', h('span', `${Math.round(job.progress * 100)} %`), h('span', job.speed ? t('install.speed', { speed: fmtSize(job.speed), eta: fmtEta(job.eta) }) : `${fmtSize(job.bytesDone)} / ${fmtSize(job.bytesTotal)}`)),
          ),
        ),
      );
    } else if (!this.idleShown) {
      this.idleShown = true;
      const face = h('div.emblem-face', h('img', { src: 'assets/brand/forge-icon.png', alt: '' }));
      this.activeEl.replaceChildren(h('div.dl-idle', coin({ face, size: 46, depth: 7, edge: ['#ffffff', '#b9c4ca'] }), h('div', h('b', t('downloads.empty')), h('small', t('downloads.emptyBody')))));
    }
    if (job) this.idleShown = false;
    this.meterEl.replaceChildren(sdMeter(this.app.sd));
  }

  setFocus(i) {
    if (!this.rowEls.length) return;
    i = clamp(i, 0, this.rowEls.length - 1);
    if (i === this.focus) return;
    this.rowEls[this.focus]?.classList.remove('focus');
    this.focus = i;
    this.rowEls[i].classList.add('focus');
    this.scroller.reveal(this.rowEls[i], 6);
    this.audio.sfx('move');
  }

  open() {
    const job = this.items[this.focus];
    if (!job) return;
    this.audio.sfx('select');
    if (job.status === 'running' || job.status === 'queued' || job.status === 'done') this.app.open('install', { job });
    else this.app.open('detail', { app: job.app });
  }

  leaveScene() {
    this.audio.sfx('back');
    this.app.back();
  }

  onButton(btn) {
    if (btn === 'UP' || btn === 'DOWN') return this.setFocus(this.focus + (btn === 'DOWN' ? 1 : -1));
    if (btn === 'A') return this.open();
    if (btn === 'X') {
      const job = this.items[this.focus];
      if (job && (job.status === 'running' || job.status === 'queued')) this.installer.cancel(job);
      return;
    }
    if (btn === 'B') return this.leaveScene();
  }

  onShow() {
    this.render();
  }

  onLanguage() {
    this.top.querySelector('.pn-title b').textContent = t('downloads.title');
    this.bottom.querySelector('.pn-bar-title').textContent = t('downloads.title');
    this.idleShown = false;
    this.render();
  }
}
