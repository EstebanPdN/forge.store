// Catalog queries shared by every scene: categories, releases, sizes, compatibility
// and the status of each app on the (virtual) SD card.
import { L } from './i18n.js';

export class Catalog {
  constructor(data, state, sd) {
    this.data = data;
    this.state = state;
    this.sd = sd;
    this.apps = data.apps;
    this.byId = new Map(data.apps.map((a) => [a.id, a]));
    this.categories = data.categories;
    this.rev = 0;
    this.listCache = new Map();
    this.listRevision = -1;
  }

  /** Adds or removes an extra source (Universal-DB) as its own tab. */
  setExtra(apps, category) {
    this.apps = [...this.data.apps, ...(apps || [])];
    this.byId = new Map(this.apps.map((a) => [a.id, a]));
    this.categories = apps ? [...this.data.categories, category] : this.data.categories;
    this.rev++;
  }

  get(id) {
    return this.byId.get(id);
  }

  list(category = 'all') {
    if (this.listRevision !== this.rev) {
      this.listRevision = this.rev;
      this.listCache.clear();
    }
    if (this.listCache.has(category)) return this.listCache.get(category);
    // "All" stays the curated catalog; Universal-DB lives in its own tab.
    const apps = category === 'all' ? this.data.apps : this.apps.filter((a) => a.category === category || a.tags?.includes(category));
    const sorted = [...apps].sort(latestFirst);
    this.listCache.set(category, sorted);
    return sorted;
  }

  search(query) {
    const q = normalizeText(query);
    if (!q) return [];
    return this.apps
      .map((app) => {
        const haystack = normalizeText(
          [app.title, app.fullTitle, app.author, app.origin?.platform, L(app.tagline), app.repo].join(' '),
        );
        const title = normalizeText(app.title + ' ' + (app.fullTitle || ''));
        let score = 0;
        if (title.startsWith(q)) score = 3;
        else if (title.includes(q)) score = 2;
        else if (haystack.includes(q)) score = 1;
        return { app, score };
      })
      .filter((r) => r.score > 0)
      .sort((a, b) => b.score - a.score || latestFirst(a.app, b.app))
      .map((r) => r.app);
  }

  /** The experimental build is only offered when it is newer than stable. */
  channels(app) {
    const rel = app.releases;
    if (!rel) return [];
    const list = [];
    if (rel.stable) list.push('stable');
    if (rel.experimental && (!rel.stable || rel.experimental.date > rel.stable.date)) list.push('experimental');
    return list;
  }

  /** Picks the release for the preferred channel, falling back to whatever exists. */
  release(app, channel = this.state.get('channel')) {
    const channels = this.channels(app);
    if (!channels.length) return null;
    const pick = channels.includes(channel) ? channel : channels[0];
    return { channel: pick, ...app.releases[pick] };
  }

  /** CIA first: it installs to the HOME Menu, which is what most players expect. */
  formats(app) {
    return Object.keys(app.install || {}).sort((a, b) => (a === b ? 0 : a === 'cia' ? -1 : b === 'cia' ? 1 : 0));
  }

  format(app, preferred = this.state.get('format')) {
    const formats = this.formats(app);
    return formats.includes(preferred) ? preferred : formats[0];
  }

  /** Release asset for a step, picking the Old 3DS variant when one exists. */
  asset(app, release, key) {
    const asset = release?.assets?.[key];
    if (!asset) return null;
    if (this.state.get('console') === 'o3ds' && asset.o3ds) return asset.o3ds;
    return asset;
  }

  downloadSize(app, format = this.format(app), release = this.release(app)) {
    if (!release || !app.install?.[format]) return 0;
    return app.install[format].reduce((sum, s) => {
      if (s.type === 'downloadRelease') return sum + (this.asset(app, release, s.asset)?.size || 0);
      if (s.type === 'downloadUrl') return sum + s.size;
      return sum;
    }, 0);
  }

  resolve(path) {
    if (!path) return path;
    const vars = this.data.vars;
    return path.replace(/%(\w+)%/g, (m, name) => vars[name] ?? m);
  }

  requirement(app, ref) {
    return app.requires?.find((r) => r.id === ref) || this.data.globalRequirements.find((r) => r.id === ref);
  }

  /** Where a user-supplied file lives (folders get an example filename). */
  requirementFile(req) {
    if (!req.path) return null;
    return req.path.endsWith('/') ? req.path + (req.example || 'file.bin') : req.path;
  }

  hasRequirement(app, req) {
    if (this.state.satisfied[`${app.id}:${req.id}`]) return true;
    const file = this.requirementFile(req);
    return file ? this.sd.exists(file) : false;
  }

  missingRequirements(app) {
    return (app.requires || []).filter((req) => !this.hasRequirement(app, req));
  }

  /** Can this console run the chosen release? */
  compat(app, release) {
    const model = this.state.get('console');
    const o3ds = release?.o3ds ?? app.compat?.o3ds;
    if (model === 'o3ds' && o3ds === false) return { ok: false, reason: 'new-only' };
    if (model === 'o3ds' && o3ds === 'experimental') return { ok: true, warn: 'old-experimental' };
    return { ok: true };
  }

  /** Install status shown on icons, banners and buttons. */
  status(app, jobs) {
    const job = jobs?.find((j) => j.app.id === app.id && ['running', 'queued'].includes(j.status));
    if (job) return { kind: job.status === 'running' ? 'downloading' : 'queued', job };
    if (app.status === 'development') return { kind: 'dev' };
    const record = this.state.installed[app.id];
    if (!record) return { kind: 'available' };
    const latest = this.release(app, record.channel);
    if (latest && latest.tag !== record.version) return { kind: 'update', record, latest };
    if (this.missingRequirements(app).length) return { kind: 'needs-file', record };
    return { kind: 'installed', record };
  }

  needsRom(app) {
    return app.requires?.some((r) => r.id === 'rom');
  }
}

/** Most recent published version, including experimental; undated apps go last. */
export function updateDate(app) {
  return Object.values(app.releases || {}).reduce((latest, release) => {
    const date = /^\d{4}-\d{2}-\d{2}/.test(release?.date || '') ? release.date.slice(0, 10) : '';
    return date > latest ? date : latest;
  }, '');
}

export function latestFirst(a, b) {
  const ad = updateDate(a), bd = updateDate(b);
  return ad === bd ? (a.id < b.id ? -1 : a.id > b.id ? 1 : 0) : ad > bd ? -1 : 1;
}

export function normalizeText(s) {
  return String(s || '')
    .toLowerCase()
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .trim();
}
