// Universal-DB source: the community catalog behind Universal-Updater.
// `data/full.json` gives titles, icons, banners, versions and download sizes;
// the `.unistore` gives each app's install script. Both are served with CORS open,
// so the prototype loads them live. On 3DS the store reads the same .unistore.

const FULL = 'https://db.universal-team.net/data/full.json';
const STORE = 'https://db.universal-team.net/unistore/universal-db.unistore';
const MB = 1024 * 1024;

let cache = null;

export async function loadUniversal() {
  if (cache) return cache;
  const [full, store] = await Promise.all([FULL, STORE].map((url) => fetch(url).then((r) => (r.ok ? r.json() : Promise.reject(new Error(`${r.status} ${url}`))))));
  const scripts = new Map(store.storeContent.map((entry) => [entry.info.title, entry]));
  cache = full
    .filter((item) => parseList(item.systems).some((s) => s.toUpperCase() === '3DS'))
    .map((item) => convert(item, scripts.get(item.title)))
    .filter(Boolean)
    .sort((a, b) => (b.stars || 0) - (a.stars || 0));
  return cache;
}

/** full.json stores some arrays and objects as Python-style strings. */
function parseList(value) {
  if (Array.isArray(value)) return value;
  if (typeof value !== 'string') return [];
  return [...value.matchAll(/'([^']*)'/g)].map((m) => m[1]);
}

function parseDownloads(value) {
  if (value && typeof value === 'object') return value;
  if (typeof value !== 'string') return {};
  const out = {};
  for (const m of value.matchAll(/'([^']+)': \{'size': (\d+)/g)) out[m[1]] = { size: Number(m[2]) };
  return out;
}

function darker(hex, amount = 0.45) {
  const n = parseInt((hex || '#556070').slice(1), 16);
  const f = (v) => Math.round(v * (1 - amount)).toString(16).padStart(2, '0');
  return `#${f(n >> 16)}${f((n >> 8) & 255)}${f(n & 255)}`;
}

function notesFrom(markdown) {
  const lines = String(markdown || '')
    .split('\n')
    .map((l) => l.trim())
    .filter((l) => /^[-*] /.test(l))
    .map((l) => l.replace(/^[-*] /, '').replace(/\*\*|`|\[([^\]]+)\]\([^)]+\)/g, '$1'))
    .filter((l) => l.length > 3)
    .slice(0, 6);
  return lines.length ? lines : [];
}

function convert(item, entry) {
  const downloads = parseDownloads(item.downloads);
  const install = {};
  if (entry) {
    for (const [key, value] of Object.entries(entry)) {
      if (key === 'info') continue;
      const script = Array.isArray(value) ? value : value.script;
      if (!Array.isArray(script) || /nightly|\[git\]|\[prerelease\]/i.test(key)) continue;
      const format = /\.cia$/i.test(key) ? 'cia' : /\.3dsx$/i.test(key) ? '3dsx' : null;
      if (!format || install[format]) continue;
      const steps = script.map((step) => convertStep(step, downloads)).filter(Boolean);
      if (steps.some((s) => s.type === 'downloadUrl')) install[format] = steps;
    }
  }
  if (!Object.keys(install).length) return null;

  const color = item.color || '#556070';
  const notes = notesFrom(item.update_notes_md);
  const size = Object.values(install)[0].filter((s) => s.type === 'downloadUrl').reduce((sum, s) => sum + s.size, 0);
  const date = (item.updated || '').slice(0, 10) || '2025-01-01';
  return {
    id: 'udb-' + item.slug,
    title: item.title,
    category: 'universal',
    source: 'universal',
    author: item.author || '—',
    color,
    status: 'released',
    repo: item.github || null,
    stars: item.stars ? Number(item.stars) : null,
    license: item.license_name || item.license || null,
    icon: item.icon || { glyph: 'box', bg: [color, darker(color)], fg: '#ffffff' },
    pixelIcon: true,
    banner: item.image ? { bg: item.image, bgPos: '50% 50%', pixel: true } : { gradient: [darker(color, 0.7), color, '#ffffff'], fx: 'pixels' },
    shots: [],
    tagline: { en: item.description || '' },
    description: { en: item.description || '' },
    features: { en: [] },
    compat: { o3ds: true, n3ds: true, stereo: false },
    folder: '/3ds/',
    notice: entry?.info?.preinstall_message ? { en: entry.info.preinstall_message } : undefined,
    releases: {
      stable: {
        tag: item.version || 'latest',
        date,
        assets: {},
        size,
        notes: { en: notes.length ? notes : ['—'] },
      },
    },
    requires: [],
    install,
  };
}

function guessSize(name, downloads) {
  if (!name) return 2 * MB;
  const base = name.split('/').pop();
  if (downloads[base]) return downloads[base].size;
  try {
    const re = new RegExp(name);
    const hit = Object.entries(downloads).find(([n]) => re.test(n));
    if (hit) return hit[1].size;
  } catch {}
  const values = Object.values(downloads);
  return values.length ? values[0].size : 2 * MB;
}

/** Maps UniStore steps onto the store's installer steps. */
function convertStep(step, downloads) {
  const out = (p) => (p || '').replace(/^sdmc:/, '');
  switch (step.type) {
    case 'downloadFile':
      return { type: 'downloadUrl', name: step.file.split('/').pop().split('?')[0], url: step.file, output: out(step.output), size: guessSize(step.file.split('/').pop(), downloads) };
    case 'downloadRelease':
      return {
        type: 'downloadUrl',
        name: out(step.output).split('/').pop(),
        url: `https://github.com/${step.repo}/releases/latest`,
        output: out(step.output),
        size: guessSize(step.file, downloads),
      };
    case 'extractFile': {
      const target = out(step.output);
      const archive = out(step.file).split('/').pop();
      const path = target.endsWith('/') ? target + archive.replace(/\.(zip|7z|rar|tar\.gz|tar)$/i, '') : target;
      return { type: 'extractFile', file: out(step.file), output: target, creates: [{ path, size: Math.round(guessSize(archive, downloads) * 1.4) }] };
    }
    case 'installCia':
    case 'deleteFile':
      return { type: step.type, file: out(step.file) };
    case 'mkdir':
      return { type: 'mkdir', directory: out(step.directory) };
    case 'rmdir':
      return { type: 'rmdir', directory: out(step.directory) };
    case 'move':
      return { type: 'move', old: out(step.old), new: out(step.new) };
    default:
      return null; // promptMessage, skip: interactive choices the prototype doesn't model
  }
}
