// Renders the prototype's visual pieces 1:1 into PNGs for the native 3DS build:
// screen backgrounds, brand text, emblem, app icons, UI icons, button badges and
// banner layers. Needs the prototype server running (tools/serve.py on :8642).
// Usage: node 3ds/tools/render-assets.mjs <out-dir>
import { chromium } from 'playwright';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';

const OUT = process.argv[2];
const BASE = process.env.FORGE_URL || 'http://localhost:8642';
mkdirSync(`${OUT}/icons`, { recursive: true });
mkdirSync(`${OUT}/ui`, { recursive: true });
mkdirSync(`${OUT}/banners`, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 900, height: 600 }, deviceScaleFactor: 1 });
page.on('pageerror', (e) => console.error('[page]', e.message));
await page.goto(`${BASE}/3ds/tools/render.html`, { waitUntil: 'networkidle' });
const ids = await page.evaluate(() => R.load());

async function shot(fn, args, file, transparent = true) {
  const handle = await page.evaluateHandle(({ fn, args }) => R[fn](...args), { fn, args });
  const el = handle.asElement();
  if (!el) return false;
  await page.waitForTimeout(30);
  await el.screenshot({ path: file, omitBackground: transparent });
  return true;
}

// Backgrounds with a 7 px margin so the dot grid can drift like on the site.
for (const theme of ['light', 'dark']) {
  await shot('bg', [theme, 407, 247], `${OUT}/bg-top-${theme}.png`, false);
  await shot('bg', [theme, 327, 247], `${OUT}/bg-bottom-${theme}.png`, false);
}

// Brand text in white; the 3DS tints it for each theme.
const brand = "font-family:'DM Sans';font-size:31px;letter-spacing:-1.3px;font-weight:400";
await shot('text', ['esteban’s <b style="font-weight:750">forge.</b>', { font: "400 31px 'DM Sans'", letterSpacing: '-1.3px' }], `${OUT}/title.png`);
await shot('text', ['WELCOME', { font: "800 7px 'Nunito'", letterSpacing: '2.6px' }], `${OUT}/welcome.png`);
await shot('text', ['<b style="font-weight:800">forge.</b>store', { font: "400 9.5px 'Nunito'", letterSpacing: '-0.1px' }], `${OUT}/wordmark.png`);
void brand;
await shot('emblem', [76], `${OUT}/emblem.png`);
await shot('emblem', [34], `${OUT}/emblem-small.png`);

// Banner layers. Transparent layers are trimmed to their content; layers.json keeps
// where each one sits on the 400x240 screen.
const layers = {};
for (const id of ids) {
  await shot('appIcon', [id, 48], `${OUT}/icons/${id}.png`);
  layers[id] = {};
  for (const layer of ['bg', 'mid', 'logo']) {
    const file = `${OUT}/banners/${id}-${layer}.png`;
    const ok = await shot('bannerLayer', [id, layer], file, layer !== 'bg');
    if (!ok) continue;
    if (layer === 'bg') {
      layers[id].bg = { x: 0, y: 0, w: 400, h: 240 };
      continue;
    }
    const box = await page.evaluate((b64) => R.bbox(b64), readFileSync(file).toString('base64'));
    if (box) layers[id][layer] = box;
  }
}
writeFileSync(`${OUT}/layers.json`, JSON.stringify(layers, null, 1));
for (const b of ['A', 'B', 'X', 'Y', 'L', 'R', 'START', 'SELECT']) await shot('badge', [b], `${OUT}/ui/badge-${b}.png`);

const UI = ['search', 'download', 'sd', 'settings', 'github', 'grid', 'anvil', 'people', 'gamepad', 'wrench', 'check', 'star', 'warn', 'hammer',
  'rom', 'file', 'depth', 'back', 'next', 'folder', 'folderPlus', 'home', 'globe', 'clock', 'update', 'trash', 'play', 'close', 'volume', 'music',
  'sun', 'moon', 'language', 'cpu', 'cia', 'shield', 'refresh', 'sparkle', 'info', 'box', 'wifi'];
for (const name of UI) await shot('uiIcon', [name, 16], `${OUT}/ui/${name}.png`);

console.log(`rendered ${ids.length} apps, ${UI.length} ui icons`);
await browser.close();
