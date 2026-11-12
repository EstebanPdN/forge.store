// Virtual SD card. Paths use the 3DS convention without the "sdmc:" prefix.
// The installer creates folders and files here exactly as it would on hardware.
import { Emitter } from './state.js';

const GB = 1024 ** 3;
const MB = 1024 ** 2;
export const SD_CAPACITY = 29.7 * GB; // a "32 GB" card after formatting

export function splitPath(path) {
  return path.split('/').filter(Boolean);
}
export function normalize(path) {
  return '/' + splitPath(path).join('/');
}
export function parentOf(path) {
  const parts = splitPath(path);
  parts.pop();
  return '/' + parts.join('/');
}
export function baseName(path) {
  const parts = splitPath(path);
  return parts[parts.length - 1] || '/';
}

function dir(meta = {}, children = {}) {
  return { type: 'dir', date: meta.date || '2026-10-02', kind: meta.kind || 'other', by: meta.by, fixed: meta.fixed, children };
}
function file(size, meta = {}) {
  return { type: 'file', size, date: meta.date || '2026-10-02', kind: meta.kind || 'other', by: meta.by };
}

/** A typical homebrew SD card: Luma3DS, the Homebrew Launcher and this store. */
export function defaultTree() {
  return dir({ kind: 'system' }, {
    '3ds': dir({ kind: 'other' }, {
      'forge-store': dir({ kind: 'store', by: 'store' }, {
        'forge-store.3dsx': file(3.4 * MB, { kind: 'store', by: 'store' }),
        cache: dir({ kind: 'store', by: 'store' }, {
          'catalog.json': file(96 * 1024, { kind: 'store', by: 'store' }),
          'icons.t3x': file(410 * 1024, { kind: 'store', by: 'store' }),
        }),
        tmp: dir({ kind: 'store', by: 'store' }),
      }),
      'dspfirm.cdc': file(30580, { kind: 'user' }),
    }),
    luma: dir({ kind: 'system' }, {
      'config.ini': file(1180, { kind: 'system' }),
      payloads: dir({ kind: 'system' }),
    }),
    'Nintendo 3DS': dir({ kind: 'system', fixed: 9.62 * GB }),
    DCIM: dir({ kind: 'other', fixed: 212 * MB }),
    'boot.firm': file(265 * 1024, { kind: 'system' }),
    'boot.3dsx': file(355 * 1024, { kind: 'system' }),
  });
}

export class SDCard extends Emitter {
  constructor(data) {
    super();
    this.root = data || defaultTree();
  }

  toJSON() {
    return this.root;
  }

  reset() {
    this.root = defaultTree();
    this.emit('change', { path: '/' });
  }

  get(path) {
    let node = this.root;
    for (const part of splitPath(path)) {
      if (node?.type !== 'dir') return null;
      node = node.children[part];
    }
    return node || null;
  }

  exists(path) {
    return !!this.get(path);
  }

  /** Creates every missing folder in the path. Returns the folders it created. */
  mkdir(path, meta = {}) {
    const created = [];
    let node = this.root;
    let current = '';
    for (const part of splitPath(path)) {
      current += '/' + part;
      if (!node.children[part]) {
        node.children[part] = dir({ ...meta, date: today() });
        created.push(current);
      }
      node = node.children[part];
      if (node.type !== 'dir') throw new Error(`${current} is a file`);
    }
    if (created.length) this.emit('change', { path });
    return created;
  }

  writeFile(path, size, meta = {}) {
    const created = this.mkdir(parentOf(path), meta);
    const parent = this.get(parentOf(path));
    parent.children[baseName(path)] = file(size, { ...meta, date: today() });
    this.emit('change', { path });
    return created;
  }

  remove(path) {
    const parent = this.get(parentOf(path));
    if (!parent || parent.type !== 'dir') return false;
    const name = baseName(path);
    if (!parent.children[name]) return false;
    delete parent.children[name];
    this.emit('change', { path });
    return true;
  }

  list(path) {
    const node = this.get(path);
    if (!node || node.type !== 'dir') return [];
    return Object.entries(node.children)
      .map(([name, child]) => ({ name, path: normalize(path + '/' + name), node: child }))
      .sort((a, b) => (a.node.type === b.node.type ? a.name.localeCompare(b.name) : a.node.type === 'dir' ? -1 : 1));
  }

  size(node = this.root) {
    if (!node) return 0;
    if (node.type === 'file') return node.size;
    if (node.fixed) return node.fixed;
    return Object.values(node.children).reduce((sum, child) => sum + this.size(child), 0);
  }

  /** Bytes used by things the store installed (for the capacity bar). */
  storeBytes(node = this.root) {
    if (!node) return 0;
    if (node.type === 'file') return node.by && node.by !== 'store' && node.kind === 'app' ? node.size : 0;
    return Object.values(node.children).reduce((sum, child) => sum + this.storeBytes(child), 0);
  }

  used() {
    return this.size(this.root);
  }

  /** Bytes of the files that match a test, e.g. everything the player copied. */
  bytes(test, node = this.root) {
    if (!node) return 0;
    if (node.type === 'file') return test(node) ? node.size : 0;
    return Object.values(node.children).reduce((sum, child) => sum + this.bytes(test, child), 0);
  }

  free() {
    return SD_CAPACITY - this.used();
  }
}

function today() {
  return new Date().toISOString().slice(0, 10);
}
