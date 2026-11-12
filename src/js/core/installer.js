// Runs an app's install script against the virtual SD card: query GitHub, create
// folders, download, verify, extract, install CIAs and check the files the player
// must provide. Downloads are simulated with realistic sizes and 3DS Wi-Fi speeds.
import { Emitter } from './state.js';
import { L } from './i18n.js';
import { baseName } from './sd.js';
import { forgeMode } from './forge-mode.js';

const MB = 1024 * 1024;

export class Installer extends Emitter {
  constructor({ catalog, sd, state }) {
    super();
    this.catalog = catalog;
    this.sd = sd;
    this.state = state;
    this.jobs = [];
    this.seq = 0;
    this.running = null;
  }

  enqueue(app, { format, channel }) {
    const release = this.catalog.release(app, channel);
    const job = {
      id: ++this.seq,
      app,
      format,
      channel: release.channel,
      release,
      tag: release.tag,
      status: 'queued',
      steps: [],
      bytesDone: 0,
      bytesTotal: 0,
      progress: 0,
      speed: 0,
      eta: Infinity,
      created: [],
      personalCreated: [],
      dirs: [],
      missing: [],
      warnings: [],
      cia: false,
    };
    job.steps = this.plan(job);
    job.bytesTotal = job.steps.reduce((sum, s) => sum + (s.total || 0), 0);
    this.jobs.push(job);
    this.emit('change', job);
    this.pump();
    return job;
  }

  plan(job) {
    const { app, format, release } = job;
    const raw = forgeMode.plan(app.id, app.install[format]);
    const steps = [];
    if (raw.some((s) => s.type === 'downloadRelease' || s.type === 'downloadUrl')) {
      steps.push({ type: 'resolve', key: 'install.step.resolve', vars: { repo: app.repo || 'Universal-DB' }, weight: 0.6 });
    }
    for (const step of raw) {
      const s = { type: step.type, raw: step, weight: 0.4 };
      const path = this.catalog.resolve(step.directory || step.file || step.output);
      switch (step.type) {
        case 'mkdir':
          Object.assign(s, { key: 'install.step.mkdir', vars: { path }, path, weight: 0.2 });
          break;
        case 'downloadRelease': {
          const asset = this.catalog.asset(app, release, step.asset);
          const output = this.catalog.resolve(step.output);
          Object.assign(s, {
            key: 'install.step.download',
            vars: { name: asset.name },
            asset,
            output,
            total: asset.size,
            weight: Math.max(0.6, asset.size / MB),
          });
          break;
        }
        case 'downloadUrl': {
          const output = this.catalog.resolve(step.output);
          Object.assign(s, {
            type: 'downloadRelease',
            key: 'install.step.download',
            vars: { name: step.name },
            output,
            total: step.size,
            weight: Math.max(0.6, step.size / MB),
          });
          break;
        }
        case 'move':
          Object.assign(s, { key: 'install.step.move', vars: { name: baseName(step.new) }, from: this.catalog.resolve(step.old), to: this.catalog.resolve(step.new), weight: 0.2 });
          break;
        case 'rmdir':
          Object.assign(s, { key: 'install.step.rmdir', vars: { path }, path, weight: 0.2 });
          break;
        case 'verifySha256':
          Object.assign(s, { key: 'install.step.verify', vars: {}, path });
          break;
        case 'extractFile': {
          const creates = (step.creates || []).map((c) => ({ ...c, path: this.catalog.resolve(c.path) }));
          const bytes = creates.reduce((sum, c) => sum + c.size, 0);
          Object.assign(s, {
            key: 'install.step.extract',
            vars: { name: baseName(path) },
            path,
            creates,
            weight: Math.max(0.6, bytes / (6 * MB)),
          });
          break;
        }
        case 'installCia':
          Object.assign(s, { key: 'install.step.installCia', vars: {}, path, weight: 1.6 });
          break;
        case 'deleteFile':
          Object.assign(s, { key: 'install.step.delete', vars: { name: baseName(path) }, path, weight: 0.15 });
          break;
        case 'dumpDsp':
          Object.assign(s, { key: 'install.step.dsp', vars: {}, weight: 0.5 });
          break;
        case 'requireFile':
        case 'checkFile': {
          const req = this.catalog.requirement(app, step.ref);
          Object.assign(s, {
            key: step.type === 'requireFile' ? 'install.step.require' : 'install.step.check',
            vars: { label: L(req.label) },
            req,
            weight: 0.3,
          });
          break;
        }
        default:
          break;
      }
      steps.push(s);
    }
    for (const s of steps) Object.assign(s, { status: 'pending', progress: 0 });
    return steps;
  }

  active() {
    return this.jobs.filter((j) => j.status === 'running' || j.status === 'queued');
  }

  find(appId) {
    return [...this.jobs].reverse().find((j) => j.app.id === appId);
  }

  cancel(job) {
    if (job.status === 'queued') {
      job.status = 'cancelled';
      this.emit('change', job);
      this.emit('done', job);
    } else if (job.status === 'running') {
      job.cancelled = true;
    }
  }

  async pump() {
    if (this.running) return;
    const next = this.jobs.find((j) => j.status === 'queued');
    if (!next) return;
    this.running = next;
    await this.run(next);
    this.running = null;
    this.pump();
  }

  async run(job) {
    job.status = 'running';
    job.startedAt = Date.now();
    this.emit('change', job);
    const totalWeight = job.steps.reduce((sum, s) => sum + s.weight, 0);
    let doneWeight = 0;
    this.updateProgress = () => {
      const active = job.steps.find((s) => s.status === 'active');
      job.progress = Math.min(1, (doneWeight + (active ? active.weight * active.progress : 0)) / totalWeight);
      this.emit('progress', job);
    };

    for (const step of job.steps) {
      if (job.cancelled) break;
      step.status = 'active';
      this.emit('step', { job, step });
      try {
        await this.exec(job, step);
      } catch (error) {
        console.error(error);
        step.status = 'error';
        job.status = 'error';
        break;
      }
      if (job.cancelled) break;
      step.progress = 1;
      if (step.status === 'active') step.status = 'done';
      doneWeight += step.weight;
      this.updateProgress();
      this.emit('step', { job, step });
    }

    job.finishedAt = Date.now();
    job.speed = 0;
    if (job.cancelled) {
      this.rollback(job);
      job.status = 'cancelled';
    } else if (job.status === 'error') {
      this.rollback(job);
    } else if (job.status !== 'error') {
      job.status = 'done';
      job.progress = 1;
      this.record(job);
    }
    this.state.save(this.sd);
    this.emit('change', job);
    this.emit('done', job);
  }

  async exec(job, step) {
    const { app } = job;
    const meta = { by: app.id, kind: 'app' };
    const tmp = this.catalog.resolve('%TMP%');
    switch (step.type) {
      case 'resolve':
        await this.tick(job, step, 650 + Math.random() * 350);
        step.result = { key: 'install.step.resolved', vars: { tag: job.tag } };
        break;
      case 'mkdir':
        await this.tick(job, step, 260);
        job.dirs.push(...this.sd.mkdir(step.path, meta));
        break;
      case 'downloadRelease':
        if (step.raw?.personal && this.sd.exists(step.output)) break;
        await this.download(job, step);
        if (job.cancelled) return;
        job.dirs.push(...this.sd.writeFile(step.output, step.total, step.raw?.personal ? { kind: 'user' } : { ...meta, kind: step.output.startsWith(tmp) ? 'temp' : 'app' }));
        if (!step.raw?.personal) job.created.push(step.output);
        else job.personalCreated.push(step.output);
        break;
      case 'verifySha256':
        await this.tick(job, step, 520);
        step.result = { key: 'install.step.verified', vars: {} };
        break;
      case 'extractFile':
        await this.tick(job, step, 500 + step.weight * 160);
        for (const c of step.creates) {
          job.dirs.push(...this.sd.writeFile(c.path, c.size, { ...meta, kind: c.path.startsWith(tmp) ? 'temp' : 'app' }));
          job.created.push(c.path);
        }
        break;
      case 'installCia':
        await this.tick(job, step, 1500 + Math.random() * 700);
        job.cia = true;
        break;
      case 'deleteFile':
        await this.tick(job, step, 180);
        this.sd.remove(step.path);
        job.created = job.created.filter((p) => p !== step.path);
        break;
      case 'move': {
        await this.tick(job, step, 220);
        const node = this.sd.get(step.from);
        if (node) {
          this.sd.remove(step.from);
          job.dirs.push(...this.sd.writeFile(step.to, node.size || 0, meta));
          job.created = job.created.filter((p) => p !== step.from);
          job.created.push(step.to);
        }
        break;
      }
      case 'rmdir':
        await this.tick(job, step, 200);
        if (this.sd.get(step.path)?.by === app.id) this.sd.remove(step.path);
        break;
      case 'dumpDsp': {
        // Audio needs the console's DSP firmware; dump it like DSP1 does when missing.
        const dsp = this.catalog.requirement(app, 'dspfirm');
        if (this.sd.exists(dsp.path)) {
          await this.tick(job, step, 380);
          step.result = { key: 'install.step.dspExists', vars: {} };
        } else {
          await this.tick(job, step, 1300);
          this.sd.writeFile(dsp.path, dsp.size, { kind: 'system', by: 'store' });
          step.result = { key: 'install.step.dspDumped', vars: {} };
        }
        break;
      }
      case 'requireFile':
      case 'checkFile':
        await this.tick(job, step, 420);
        if (this.catalog.hasRequirement(app, step.req)) {
          step.result = { key: 'install.step.found', vars: {} };
        } else {
          step.status = 'warn';
          const where = this.catalog.requirementFile(step.req);
          step.result = where
            ? { key: 'install.step.missing', vars: { path: where } }
            : { key: 'install.step.missingAny', vars: { help: L(step.req.help) } };
          (step.type === 'requireFile' ? job.missing : job.warnings).push(step.req);
        }
        break;
      default:
        await this.tick(job, step, 200);
    }
  }

  /** Animated wait for steps without bytes. */
  async tick(job, step, ms) {
    const start = performance.now();
    while (!job.cancelled) {
      const p = Math.min(1, (performance.now() - start) / ms);
      step.progress = p;
      this.updateProgress();
      if (p >= 1) break;
      await sleep(50);
    }
  }

  async download(job, step) {
    const base = (this.state.get('console') === 'o3ds' ? 0.85 : 1.2) * MB * this.state.get('speed');
    let done = 0;
    let wave = Math.random() * 10;
    let last = performance.now();
    while (done < step.total && !job.cancelled) {
      await sleep(80);
      const now = performance.now();
      const dt = (now - last) / 1000;
      last = now;
      wave += dt * 1.7;
      const speed = base * (0.78 + 0.22 * Math.sin(wave) + (Math.random() - 0.5) * 0.25);
      const chunk = Math.min(step.total - done, speed * dt);
      done += chunk;
      job.bytesDone += chunk;
      job.speed = job.speed ? job.speed * 0.8 + speed * 0.2 : speed;
      job.eta = (job.bytesTotal - job.bytesDone) / job.speed;
      step.progress = done / step.total;
      this.updateProgress();
    }
  }

  record(job) {
    const previous = this.state.installed[job.app.id];
    this.state.installed[job.app.id] = {
      version: job.tag,
      channel: job.channel,
      format: job.format,
      date: new Date().toISOString().slice(0, 10),
      files: [...new Set([...(previous?.files || []), ...job.created])],
      dirs: [...new Set([...(previous?.dirs || []), ...job.dirs])],
      cia: job.cia || previous?.cia || false,
    };
  }

  /** Removes what a cancelled job had already written. */
  rollback(job) {
    for (const path of job.created) this.sd.remove(path);
    for (const path of job.personalCreated) this.sd.remove(path);
    for (const path of [...job.dirs].reverse()) {
      if (!this.sd.list(path).length) this.sd.remove(path);
    }
  }

  uninstall(app) {
    const record = this.state.installed[app.id];
    if (!record) return;
    for (const path of record.files) {
      const node = this.sd.get(path);
      if (node && node.by === app.id && node.kind !== 'user') this.sd.remove(path);
    }
    for (const path of [...record.dirs].sort((a, b) => b.length - a.length)) {
      if (this.sd.exists(path) && !this.sd.list(path).length) this.sd.remove(path);
    }
    delete this.state.installed[app.id];
    this.state.save(this.sd);
    this.emit('change', null);
    this.emit('uninstall', app);
  }
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
