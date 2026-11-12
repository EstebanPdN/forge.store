// Boots the prototype: loads the catalog, builds the services, wires the design
// toolbar and waits for the first click to "power on" the console.
import { $, $$ } from './core/dom.js';
import { setLang, t, detectLang, LANGUAGES } from './core/i18n.js';
import { State } from './core/state.js';
import { SDCard } from './core/sd.js';
import { Catalog } from './core/catalog.js';
import { Installer } from './core/installer.js';
import { AudioEngine } from './core/audio.js';
import { Input } from './core/input.js';
import { refreshFromGitHub } from './core/github.js';
import { loadUniversal } from './core/universal.js';
import { forgeMode } from './core/forge-mode.js';
import { App } from './app.js';
import { IntroScene } from './scenes/intro.js';
import { HomeScene } from './scenes/home.js';
import { DetailScene } from './scenes/detail.js';
import { InstallScene } from './scenes/install.js';
import { DownloadsScene } from './scenes/downloads.js';
import { SearchScene } from './scenes/search.js';
import { SettingsScene } from './scenes/settings.js';
import { SDScene } from './scenes/sdcard.js';
import { HomeMenuScene } from './scenes/homemenu.js';
import { GitHubScene } from './scenes/github.js';
import { NativePreview } from './ui/native-preview.js';

const VIEW_SIZE = {
  console: [520, 650],
  screens: [410, 508],
  side: [752, 250],
};

async function main() {
  const state = new State();
  const root = document.documentElement;
  root.dataset.theme = state.get('theme');
  // First launch: follow the console's language.
  if (!state.get('lang')) state.settings.lang = detectLang();
  setLang(state.get('lang'));

  const device = $('#device');
  const top = $('#screen-top');
  const bottom = $('#screen-bottom');

  let powered = false;
  const data = await fetch('catalog/forge-store.json', { cache: 'no-cache' }).then((r) => r.json());
  await forgeMode.ready;
  const sd = new SDCard(state.sdData);
  let saveTimer;
  sd.on('change', () => {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => state.save(sd), 300);
  });
  const catalog = new Catalog(data, state, sd);
  const installer = new Installer({ catalog, sd, state });
  const audio = new AudioEngine();
  audio.setVolume(state.get('volume'));
  audio.setMusic(state.get('music'));
  audio.setSfx(state.get('sfx'));
  audio.preload(['assets/audio/intro-welcome.mp3', 'assets/audio/intro-enter.mp3']);
  const input = new Input(device);

  const app = new App({ device, top, bottom, catalog, state, sd, installer, audio, input });
  app.register({
    intro: IntroScene,
    home: HomeScene,
    detail: DetailScene,
    install: InstallScene,
    downloads: DownloadsScene,
    search: SearchScene,
    settings: SettingsScene,
    sd: SDScene,
    homeMenu: HomeMenuScene,
    github: GitHubScene,
  });
  window.forge = app; // handy for poking at the prototype from the console

  // --- layout: view mode, zoom, shell colour, 3D slider ------------------
  function layout() {
    const view = state.get('view');
    device.dataset.view = view;
    device.dataset.shell = state.get('shell');
    const stage = $('.stage');
    const [w, h] = VIEW_SIZE[view];
    let zoom = state.get('zoom');
    if (zoom === 'auto') {
      zoom = Math.min((stage.clientWidth - 40) / w, (stage.clientHeight - 30) / h);
      zoom = Math.max(1, Math.min(4, Math.floor(zoom)));
    } else zoom = Number(zoom);
    if (state.get('nativePreview')) zoom = Math.max(1, Math.round(zoom));
    device.style.setProperty('--zoom', zoom);
    device.style.left = device.style.top = '0px';
    stage.style.overflow = 'auto';
    const bounds = device.getBoundingClientRect();
    device.style.left = `${(Math.round(bounds.left) - bounds.left) / zoom}px`;
    device.style.top = `${(Math.round(bounds.top) - bounds.top) / zoom}px`;
    app.zoom = zoom;
    dispatchEvent(new CustomEvent('forge:zoom', { detail: zoom }));
  }
  app.zoom = 1;
  addEventListener('resize', layout);

  function setDepth(value) {
    const depth = Math.max(0, Math.min(1, value));
    device.style.setProperty('--depth', depth.toFixed(2));
    device.classList.toggle('depth-on', depth > 0.02);
    state.settings.depth = depth;
  }
  setDepth(state.get('depth'));
  const slider = $('#slider3d');
  slider.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    const track = slider.querySelector('.track').getBoundingClientRect();
    const move = (ev) => setDepth(1 - (ev.clientY - track.top) / track.height);
    move(e);
    const up = () => {
      removeEventListener('pointermove', move);
      removeEventListener('pointerup', up);
      state.save();
    };
    addEventListener('pointermove', move);
    addEventListener('pointerup', up);
  });

  // --- design toolbar ------------------------------------------------------
  function syncToolbar() {
    for (const group of $$('.devbar [data-group]')) {
      const key = group.dataset.group;
      const value = String(state.get(key));
      group.querySelectorAll('[data-value]').forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.value === value)));
    }
    $('#toggle-guides').setAttribute('aria-pressed', String(state.get('guides')));
    device.classList.toggle('guides', state.get('guides'));
    const on = state.get('music') || state.get('sfx');
    $('#toggle-sound').textContent = on ? '🔊 Sonido' : '🔇 Silencio';
  }
  const langSelect = $('#dev-lang');
  langSelect.replaceChildren(...LANGUAGES.map((l) => new Option(l.name, l.id)));
  langSelect.value = state.get('lang');
  langSelect.addEventListener('change', () => state.set('lang', langSelect.value));
  $('#reset-sd').addEventListener('click', () => {
    installer.active().forEach((job) => installer.cancel(job));
    sd.reset();
    state.installed = {};
    state.satisfied = {};
    state.save(sd);
    installer.emit('uninstall', null);
    if (powered) app.toast({ text: t('toast.resetSd'), icon: 'refresh', tone: 'danger' });
  });
  $('.devbar').addEventListener('click', (e) => {
    const btn = e.target.closest('[data-value]');
    if (!btn) return;
    const key = btn.closest('[data-group]').dataset.group;
    state.set(key, btn.dataset.value);
  });
  $('#toggle-guides').addEventListener('click', () => state.set('guides', !state.get('guides')));
  $('#toggle-sound').addEventListener('click', () => {
    const on = !(state.get('music') || state.get('sfx'));
    state.set('music', on);
    state.set('sfx', on);
  });
  $('#replay-intro').addEventListener('click', () => {
    if (device.classList.contains('off')) return powerOn();
    app.reset('intro', {}, 'fade');
  });
  $('#first-run').addEventListener('click', () => {
    state.set('onboarded', false);
    if (device.classList.contains('off')) return powerOn();
    app.reset('intro', {}, 'fade');
  });

  state.on('setting', ({ key, value }) => {
    if (key === 'theme') root.dataset.theme = value;
    if (key === 'lang') {
      setLang(value);
      app.relabel();
    }
    if (key === 'music') audio.setMusic(value);
    if (key === 'sfx') audio.setSfx(value);
    if (key === 'volume') audio.setVolume(value);
    if (key === 'lang') langSelect.value = value;
    if (key === 'universalDb') universal(value);
    if (['view', 'zoom', 'shell', 'nativePreview'].includes(key)) layout();
    syncToolbar();
  });

  // Versions, sizes and stars always come live from GitHub (cached for an hour);
  // offline, the store quietly keeps the bundled catalog.
  async function live() {
    const result = await refreshFromGitHub(catalog, state);
    if (result.ok) app.relabel();
  }

  /** Universal-DB as an extra source: its own tab with the whole catalog. */
  async function universal(on) {
    if (!on) {
      return catalog.setExtra(null);
    }
    if (powered) app.toast({ text: t('udb.loading'), icon: 'globe', tone: 'forge' });
    try {
      const apps = await loadUniversal();
      if (!state.get('universalDb')) return;
      catalog.setExtra(apps, { id: 'universal', label: { en: 'Universal' }, icon: 'globe' });
      if (powered) app.toast({ text: t('udb.loaded', { n: apps.length }), icon: 'globe' });
    } catch (error) {
      console.warn('[universal-db]', error);
      if (powered) app.toast({ text: t('udb.failed'), icon: 'wifi', tone: 'amber' });
    }
  }

  // --- power on --------------------------------------------------------------
  function powerOn() {
    if (powered) return;
    powered = true;
    audio.unlock();
    device.classList.remove('off');
    document.body.classList.add('powered');
    input.enabled = true;
    app.reset('intro', {}, 'none');
    live();
    if (state.get('universalDb')) universal(true);
  }
  $('.stage').addEventListener('pointerdown', powerOn);
  addEventListener('keydown', (e) => {
    if (!powered && !e.metaKey && !e.ctrlKey) powerOn();
  });

  syncToolbar();
  app.nativePreview = new NativePreview(app);
  layout();
  requestAnimationFrame(layout);
}

main().catch((error) => {
  console.error(error);
  document.body.insertAdjacentHTML(
    'beforeend',
    `<p style="position:fixed;inset:auto 0 50% 0;text-align:center;font:600 14px system-ui">No se pudo cargar el prototipo. Ábrelo con un servidor local (./serve.command).<br><small>${error}</small></p>`,
  );
});
