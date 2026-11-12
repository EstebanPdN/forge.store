// Install progress. Top: the "forge" — the app icon inside a progress ring with
// embers that grow with the download, and a burst of sparks plus an anvil clang
// when it finishes. Bottom: every script step with its live state.
import { Scene } from './scene.js';
import { h, animate } from '../core/dom.js';
import { t, L, fmtSize, fmtEta } from '../core/i18n.js';
import { icon } from '../core/icons.js';
import { FX } from '../core/fx.js';
import { appIcon, Scroller } from '../ui/components.js';

const RING_RADIUS = 58;
const RING = 2 * Math.PI * RING_RADIUS;

export class InstallScene extends Scene {
  static section = 'install';

  build() {
    this.job = this.params.job;
    const item = this.job.app;

    // Top screen
    const art = item.banner?.bg || item.banner?.mid;
    const gradient = item.banner?.gradient;
    this.backdrop = h('div.in-backdrop', {
      style: art ? { backgroundImage: `url("${art}")` } : { background: `linear-gradient(160deg, ${gradient?.[1] || item.color}, ${gradient?.[0] || '#111'})` },
    });
    this.canvas = h('canvas.in-fx');
    this.fx = new FX(this.canvas, 400, 240);
    this.fx.resize(this.app.zoom);
    this.onZoom = (e) => this.fx.resize(e.detail);
    addEventListener('forge:zoom', this.onZoom);

    this.ringFill = h('div', {
      html: `<svg class="in-ring" viewBox="0 0 128 128"><defs><linearGradient id="ring-grad" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffb347"/><stop offset="1" stop-color="#ff5a2c"/></linearGradient></defs><circle class="track" cx="64" cy="64" r="${RING_RADIUS}"/><circle class="fill" cx="64" cy="64" r="${RING_RADIUS}" style="stroke-dasharray:${RING};stroke-dashoffset:${RING}"/></svg>`,
    }).firstChild;
    this.iconEl = appIcon(item, { size: 64 });
    this.check = h('i.in-check', icon('check'));
    this.forge = h('div.in-forge', this.ringFill, h('div.in-icon', this.iconEl, this.check));
    this.percent = h('div.in-percent', '0 %');
    this.stepLine = h('div.in-step.ellipsis');
    this.speedLine = h('div.in-speed');
    this.title = h('div.in-title');
    const rel = this.job.release;
    this.top.append(
      this.backdrop,
      h('div.in-shade'),
      this.canvas,
      h('div.in-head', this.title, h('div.in-chips', h('span.chip.dark', rel.tag), h('span.chip.dark', t('format.' + this.job.format)), h('span.chip.dark', t('channel.' + this.job.channel)))),
      this.forge,
      h('div.in-readout', this.percent, this.stepLine, this.speedLine),
    );

    // Bottom screen
    this.list = h('div.scroll-content.in-steps');
    this.scrollEl = h('div.in-scroll.scroller', this.list);
    this.scroller = new Scroller(this.scrollEl, this.list);
    this.extra = h('div.in-extra');
    this.buttons = h('div.in-buttons');
    this.counter = h('span.in-count');
    this.bottom.append(
      h('div.bg-forge'),
      h('div.in-bottom', h('div.in-bar', h('div.in-bar-title', appIcon(item, { size: 18 }), h('b.ellipsis', item.title)), this.counter), this.scrollEl, this.extra, this.buttons),
    );

    this.renderSteps();
    this.update();
    this.renderButtons();
    this.fx.setPreset(this.job.status === 'running' ? 'embers' : null);

    this.listen(this.installer, 'progress', (job) => job === this.job && this.update());
    this.listen(this.installer, 'step', ({ job, step }) => {
      if (job !== this.job) return;
      this.renderStep(step);
      this.update();
      if (step.status === 'active') this.audio.sfx('move', { pitch: 7 });
      if (step.status === 'warn') this.audio.sfx('warn');
    });
    this.listen(this.installer, 'change', (job) => {
      if (job !== this.job) return;
      if (job.status === 'running') this.fx.setPreset('embers');
      this.update();
      this.renderButtons();
    });
    this.listen(this.installer, 'done', (job) => job === this.job && this.finish());
  }

  stepLabel(step) {
    return t(step.key, step.vars);
  }

  renderSteps() {
    this.rows = new Map();
    this.list.replaceChildren(
      ...this.job.steps.map((step, i) => {
        const row = h('div.in-row', { dataset: { i } });
        this.rows.set(step, row);
        this.fillRow(row, step);
        return row;
      }),
    );
  }

  fillRow(row, step) {
    row.className = 'in-row ' + step.status;
    const stateIcon =
      step.status === 'active'
        ? h('span.spinner')
        : step.status === 'done'
          ? h('i.st.ok', icon('check'))
          : step.status === 'warn'
            ? h('i.st.warn', icon('warn'))
            : step.status === 'error'
              ? h('i.st.err', icon('close'))
              : h('i.st.idle');
    const right = [];
    if (step.status === 'active' && step.total) right.push(h('span.in-row-pct', `${Math.round(step.progress * 100)} %`));
    else if (step.total && step.status === 'done') right.push(h('span.in-row-pct.faint', fmtSize(step.total)));
    const result = step.result ? h('div.in-row-result', t(step.result.key, step.result.vars)) : null;
    const bar = step.status === 'active' && (step.total || step.type === 'extractFile' || step.type === 'installCia') ? h('div.bar', h('i', { style: { '--p': step.progress } })) : null;
    row.replaceChildren(stateIcon, h('div.in-row-text', h('div.in-row-label.ellipsis', this.stepLabel(step)), result, bar), ...right);
  }

  renderStep(step) {
    const row = this.rows.get(step);
    if (!row) return;
    this.fillRow(row, step);
    if (step.status === 'active') this.scroller.reveal(row, 14);
  }

  update() {
    const job = this.job;
    const p = job.progress || 0;
    this.ringFill.querySelector('.fill').style.strokeDashoffset = String(RING * (1 - p));
    this.percent.textContent = `${Math.round(p * 100)} %`;
    const active = job.steps.find((s) => s.status === 'active');
    if (active) {
      this.stepLine.textContent = this.stepLabel(active);
      this.renderStep(active);
    }
    const done = job.steps.filter((s) => s.status !== 'pending' && s.status !== 'active').length;
    this.counter.textContent = `${done}/${job.steps.length}`;
    if (job.status === 'queued') {
      this.title.textContent = t('install.queued');
      this.stepLine.textContent = t('downloads.ofQueue', { n: this.installer.active().indexOf(job) });
      this.speedLine.textContent = '';
      this.forge.classList.add('queued');
    } else if (job.status === 'running') {
      this.forge.classList.remove('queued');
      this.title.textContent = `${t('install.title')} ${job.app.title}`;
      const downloading = active?.type === 'downloadRelease';
      this.speedLine.textContent = downloading && job.speed ? t('install.speed', { speed: fmtSize(job.speed), eta: fmtEta(job.eta) }) : `${fmtSize(job.bytesDone)} / ${fmtSize(job.bytesTotal)}`;
      // Embers burn hotter as the bar fills.
      this.canvas.style.opacity = String(0.45 + p * 0.55);
    }
  }

  finish() {
    const job = this.job;
    this.update();
    this.renderSteps();
    this.renderButtons();
    this.fx.setPreset(null);
    if (job.status === 'done') {
      const missing = job.missing.length;
      this.title.textContent = missing ? t('install.doneMissing') : t('install.done');
      this.percent.textContent = '100 %';
      this.stepLine.textContent = t('toast.installed', { title: job.app.title });
      this.speedLine.textContent = `${fmtSize(job.bytesTotal)} · ${job.steps.length} ${this.app.localize({ es: 'pasos', en: 'steps' })}`;
      this.forge.classList.add('done');
      if (missing) this.forge.classList.add('missing');
      this.audio.sfx('anvil');
      setTimeout(() => this.audio.sfx('complete'), 120);
      this.fx.burst(200, 96, 90);
      setTimeout(() => this.fx.burst(200, 96, 40), 180);
      animate(this.iconEl, [{ transform: 'scale(1)' }, { transform: 'scale(1.18)', offset: 0.35 }, { transform: 'scale(.96)', offset: 0.7 }, { transform: 'scale(1)' }], { duration: 700, fill: 'none' });
      animate(this.check, [{ opacity: 0, transform: 'scale(0) rotate(-40deg)' }, { opacity: 1, transform: 'none' }], { duration: 520, delay: 160, easing: 'cubic-bezier(0.34, 1.56, 0.64, 1)' });
      const flash = h('div.in-flash');
      this.top.append(flash);
      animate(flash, [{ opacity: 0.85 }, { opacity: 0 }], { duration: 700, easing: 'ease-out' }).then(() => flash.remove());
      if (missing) this.renderMissing();
    } else {
      this.forge.classList.add('stopped');
      this.title.textContent = job.status === 'cancelled' ? t('install.cancelled') : t('install.failed');
      this.speedLine.textContent = '';
      this.audio.sfx('error');
    }
  }

  renderMissing() {
    const req = this.job.missing[0];
    const file = this.catalog.requirementFile(req);
    const card = h(
      'div.next-card',
      h('b', icon('warn'), t('install.nextTitle')),
      h('p', file ? t('install.nextBody', { label: L(req.label), path: 'sdmc:' + file }) : L(req.help)),
      h(
        'button.ui-btn.small',
        {
          type: 'button',
          on: {
            click: () => {
              if (file) this.app.sd.writeFile(file, req.size || 1048576, { kind: 'user' });
              else this.state.satisfied[`${this.job.app.id}:${req.id}`] = true;
              this.state.save(this.app.sd);
              this.audio.sfx('complete');
              this.app.toast({ text: t('toast.copied', { label: L(req.label) }), icon: 'sd' });
              this.forge.classList.remove('missing');
              this.title.textContent = t('install.done');
              this.extra.replaceChildren();
              this.scrollEl.style.bottom = '';
            },
          },
        },
        icon('sd'),
        t('detail.simulateCopy'),
      ),
    );
    this.extra.replaceChildren(card);
    requestAnimationFrame(() => {
      this.scrollEl.style.bottom = `${card.offsetHeight + 44}px`;
      this.scroller.syncBar();
    });
    animate(card, [{ opacity: 0, transform: 'translateY(10px)' }, { opacity: 1, transform: 'none' }], { duration: 420, delay: 300, fill: 'backwards' });
  }

  renderButtons() {
    const job = this.job;
    const running = job.status === 'running' || job.status === 'queued';
    const b = (cls, ic, label, g, fn) => h('button.ui-btn' + cls, { type: 'button', on: { click: fn } }, ic ? icon(ic) : null, h('span', label));
    this.buttons.replaceChildren(
      ...(running
        ? [b('.ghost.grow', null, t('action.keepBrowsing'), 'B', () => this.leaveScene()), b('.danger', 'close', t('action.cancel'), 'X', () => this.installer.cancel(job))]
        : job.status === 'done'
          ? [b('.ghost', 'play', t('action.open'), 'Y', () => this.launch()), b('.primary.grow', 'check', t('action.done'), 'A', () => this.leaveScene())]
          : [b('.primary.grow', null, t('action.back'), 'B', () => this.leaveScene())]),
    );
  }

  launch() {
    this.audio.sfx('launch');
    this.app.toast({ text: t('toast.opening', { title: this.job.app.title }), image: appIcon(this.job.app, { size: 18 }) });
  }

  leaveScene() {
    if (this.leaving) return;
    this.leaving = true;
    if (this.job.status === 'running' || this.job.status === 'queued') this.app.toast({ text: t('install.background'), icon: 'download', tone: 'forge' });
    this.audio.sfx('back');
    this.app.back().then(() => (this.leaving = false));
  }

  onButton(btn) {
    const running = this.job.status === 'running' || this.job.status === 'queued';
    if (btn === 'B' || (btn === 'A' && !running)) return this.leaveScene();
    if (btn === 'X' && running) return this.installer.cancel(this.job);
    if (btn === 'Y' && this.job.status === 'done') return this.launch();
    if (btn === 'UP' || btn === 'DOWN') this.scroller.by(btn === 'DOWN' ? 30 : -30);
  }

  onShow() {
    this.audio.muffle(0.55);
  }

  onLanguage() {
    this.renderSteps();
    this.renderButtons();
    this.update();
  }

  destroy() {
    super.destroy();
    this.fx.stop();
    removeEventListener('forge:zoom', this.onZoom);
  }
}
