// Persistent prototype state: settings, installed apps and the virtual SD card.
// On 3DS the same data lives in sdmc:/3ds/forge-store/ (settings.ini, installed.json).

export class Emitter {
  constructor() {
    this.listeners = new Map();
  }
  on(event, fn) {
    if (!this.listeners.has(event)) this.listeners.set(event, new Set());
    this.listeners.get(event).add(fn);
    return () => this.listeners.get(event)?.delete(fn);
  }
  emit(event, data) {
    this.listeners.get(event)?.forEach((fn) => fn(data));
    this.listeners.get('*')?.forEach((fn) => fn(event, data));
  }
}

const KEY = 'forge-store/v1';

export const DEFAULTS = {
  theme: 'light',
  onboarded: false,
  lang: null,
  music: true,
  sfx: true,
  volume: 0.8,
  format: 'cia',
  channel: 'stable',
  console: 'n3ds',
  speed: 4,
  universalDb: false,
  view: 'console',
  zoom: 'auto',
  shell: 'graphite',
  depth: 0.6,
  guides: false,
};

export class State extends Emitter {
  constructor() {
    super();
    let saved = {};
    try {
      saved = JSON.parse(localStorage.getItem(KEY)) || {};
    } catch {}
    this.settings = { ...DEFAULTS, ...saved.settings };
    // Revision 2 made CIA the default install format.
    if ((saved.rev || 0) < 2) this.settings.format = 'cia';
    this.installed = saved.installed || {};
    this.satisfied = saved.satisfied || {};
    this.sdData = saved.sd || null;
    this.liveCache = saved.liveCache || null;
  }

  save(sd) {
    if (sd) this.sdData = sd.toJSON();
    try {
      localStorage.setItem(
        KEY,
        JSON.stringify({
          rev: 2,
          settings: this.settings,
          installed: this.installed,
          satisfied: this.satisfied,
          sd: this.sdData,
          liveCache: this.liveCache,
        }),
      );
    } catch {}
  }

  set(key, value) {
    if (this.settings[key] === value) return;
    this.settings[key] = value;
    this.save();
    this.emit('setting', { key, value });
  }

  get(key) {
    return this.settings[key];
  }
}
