// Renders the prototype's 3D coins (ui/coin.js + ui.css) pose by pose, at 4x, for the
// native build: the intro emblem and the light/dark theme-picker coins. pack.py reduces
// each frame to 1x with an area filter, so edges are clean and the pixel-art face does not
// shimmer while it wobbles.
//
// Wobble: the coin-wobble keyframes sampled uniformly in progress p (0 = rotateY(-24deg),
// 1 = rotateY(24deg)); the console applies the CSS ease-in-out timing itself.
// Spin: the confirm animation's rotateY(0..360deg) in 20 degree steps (scale applied live).
// Needs the prototype server running (tools/serve.py on :8642).
// Usage: node 3ds/tools/render-coins.mjs <out-dir>
import { chromium } from 'playwright';
import { mkdirSync, writeFileSync } from 'node:fs';

const OUT = process.argv[2];
const BASE = process.env.FORGE_URL || 'http://localhost:8642';
const WOBBLE = 17;
const SPIN = 18;
mkdirSync(OUT, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 400, height: 300 }, deviceScaleFactor: 4 });
page.on('pageerror', (e) => console.error('[page]', e.message));
await page.goto(`${BASE}/3ds/tools/render.html`, { waitUntil: 'networkidle' });
await page.evaluate(() => R.load());

const COINS = {
  emblem: { size: 76, depth: 12, edge: ['#ffffff', '#b9c4ca'], scale: 1, face: 'emblem' },
  light: { size: 80, depth: 10, edge: ['#ffffff', '#d6ccb0'], scale: 1.06, face: 'light' },
  dark: { size: 80, depth: 10, edge: ['#6b717d', '#1f2125'], scale: 1.06, face: 'dark' },
  about: { size: 34, depth: 6, edge: ['#ffffff', '#b9c4ca'], scale: 1, face: 'emblem' },  // settings card
};

const meta = {};
for (const [name, c] of Object.entries(COINS)) {
  // Box with room for the perspective overflow; the coin's centre is (cx, cy) in it.
  const S = Math.ceil(c.size * c.scale);
  const W = S + 16, H = S + 10;
  await page.evaluate(
    async ({ c, W, H }) => {
      const { coin } = await import('/src/js/ui/coin.js');
      const { icon } = await import('/src/js/core/icons.js');
      const { h } = await import('/src/js/core/dom.js');
      let face;
      if (c.face === 'emblem') {
        const img = h('img', { src: '/assets/brand/forge-icon.png', style: { imageRendering: 'auto' } });
        face = h('div.emblem-face', img);
        await img.decode();
      } else {
        face = h('div.theme-face.' + c.face, icon(c.face === 'light' ? 'sun' : 'moon'));
      }
      const el = coin({ face, size: c.size, depth: c.depth, edge: c.edge });
      el.querySelector('.coin-shadow').remove();
      const holder = h('div', { style: { position: 'absolute', left: '50%', top: '50%', width: `${c.size}px`, height: `${c.size}px`,
        margin: `${-c.size / 2}px 0 0 ${-c.size / 2}px`, transform: `scale(${c.scale})` } }, el);
      const box = h('div', { id: 'coinbox', style: { position: 'relative', width: `${W}px`, height: `${H}px` } }, holder);
      // The prototype page sizes every box with border-box (page.css); render.html doesn't load it.
      for (const n of [box, ...box.querySelectorAll('*')]) n.style.boxSizing = 'border-box';
      document.getElementById('stage').replaceChildren(box);
      for (const n of box.querySelectorAll('.coin-body, .coin-shine')) n.style.setProperty('animation', 'none', 'important');
    },
    { c, W, H },
  );
  const box = page.locator('#coinbox');
  const pose = (transform, shine) =>
    page.evaluate(
      ({ transform, shine }) => {
        document.querySelector('#coinbox .coin-body').style.setProperty('transform', transform, 'important');
        document.querySelector('#coinbox .coin-shine').style.setProperty('background-position', shine, 'important');
      },
      { transform, shine },
    );
  for (let i = 0; i < WOBBLE; i++) {
    const p = i / (WOBBLE - 1);
    const L = (a, b) => (a + (b - a) * p).toFixed(3);
    await pose(`rotateY(${L(-24, 24)}deg) rotateX(${L(6, -4)}deg) rotateZ(${L(-2, 2)}deg)`, `${L(95, 5)}% 0`);
    await box.screenshot({ path: `${OUT}/${name}-w${String(i).padStart(2, '0')}.png`, omitBackground: true });
  }
  if (name === 'light' || name === 'dark') {
    for (let i = 0; i < SPIN; i++) {
      await pose(`rotateY(${(i * 360) / SPIN}deg)`, '50% 0');
      await box.screenshot({ path: `${OUT}/${name}-s${String(i).padStart(2, '0')}.png`, omitBackground: true });
    }
  }
  meta[name] = { w: W, h: H, cx: W / 2, cy: H / 2, size: c.size * c.scale, wobble: WOBBLE, spin: name === 'light' || name === 'dark' ? SPIN : 0 };
  console.log(`${name}: ${W}x${H}`);
}

// Halo behind the emblem and the soft contact shadow under a coin (white, tinted live).
await page.evaluate(() => {
  const { style } = document.getElementById('stage');
  document.getElementById('stage').innerHTML =
    '<div id="halo" style="width:108px;height:108px;border-radius:50%;background:radial-gradient(circle, rgba(255,210,31,.35), transparent 65%)"></div>' +
    '<div id="shadow" style="width:56px;height:9px;border-radius:50%;background:radial-gradient(closest-side, #fff, transparent)"></div>' +
    // Status-bar emblem: 18 px with a 1 px white ring (.sb-pill img), smooth instead of pixelated.
    // Keep in step with render-sb-emblem.mjs, which renders only this piece.
    '<div id="sb" style="width:18px;height:18px;box-sizing:border-box;border-radius:50%;border:1px solid #fff;overflow:hidden;background:#fff">' +
    '<img src="/assets/brand/forge-icon.png" style="display:block;width:100%;height:100%"></div>';
  void style;
});
await page.locator('#halo').screenshot({ path: `${OUT}/halo.png`, omitBackground: true });
await page.locator('#shadow').screenshot({ path: `${OUT}/shadow.png`, omitBackground: true });
await page.evaluate(() => document.querySelector('#sb img').decode());
await page.locator('#sb').screenshot({ path: `${OUT}/sb-emblem.png`, omitBackground: true });
writeFileSync(`${OUT}/coins.json`, JSON.stringify(meta, null, 1));

// Settings tiles with their gradient and coloured glow (.st-tile): 52 px (hero) and 36 px (console).
const TILE_COLORS = ['#59606a', '#f2a100', '#2f8fe0', '#7c6fd6', '#9b6fd6', '#ff8a1f', '#4f9e86', '#e5484d'];
const tiles = { pad: 18, names: [] };
await page.addStyleTag({ url: '/src/css/panels.css' });
for (const [kind, size] of [['hero', 52], ['console', 36]]) {
  for (const c of TILE_COLORS) {
    await page.evaluate(
      ({ c, size, pad }) => {
        document.getElementById('stage').innerHTML =
          `<div id="tile" style="box-sizing:border-box;padding:${pad}px;width:${size + pad * 2}px;height:${size + pad * 2}px">` +
          `<span class="st-tile" style="--c:${c};width:${size}px;height:${size}px;border-radius:${size === 52 ? 15 : 11}px;box-sizing:border-box"></span></div>`;
      },
      { c, size, pad: tiles.pad },
    );
    const name = `${kind}-${c.slice(1)}`;
    await page.locator('#tile').screenshot({ path: `${OUT}/tile-${name}.png`, omitBackground: true });
    tiles.names.push(name);
  }
}
writeFileSync(`${OUT}/tiles.json`, JSON.stringify(tiles));
await browser.close();
