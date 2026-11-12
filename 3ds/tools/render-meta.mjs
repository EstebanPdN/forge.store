// HOME Menu art for the CIA: 48x48 icon and the 256x128 banner, drawn with the
// prototype's fonts and colours. The banner sound is the intro's welcome chime.
// Needs the prototype server running (tools/serve.py on :8642).
// Usage: node 3ds/tools/render-meta.mjs <out-dir>
import { chromium } from 'playwright';
import { execFileSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';

const OUT = process.argv[2];
const BASE = process.env.FORGE_URL || 'http://localhost:8642';
mkdirSync(OUT, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 600, height: 400 }, deviceScaleFactor: 1 });
await page.goto(`${BASE}/3ds/tools/render.html`, { waitUntil: 'networkidle' });
await page.evaluate(() => R.load());

async function render(html, file) {
  await page.evaluate((html) => {
    const stage = document.getElementById('stage');
    stage.innerHTML = html;
  }, html);
  await page.evaluate(() => Promise.all([...document.images].map((i) => i.decode().catch(() => {}))));
  await page.waitForTimeout(50);
  await page.locator('#stage > div').screenshot({ path: file, omitBackground: true });
}

// Icon: the forge character in crisp 2x pixels on its yellow field (the HOME Menu adds the frame).
await render(
  `<div style="width:48px;height:48px;background:#ffd400;display:grid;place-items:center">
     <img src="/assets/brand/forge-icon.png" style="width:44px;height:44px;image-rendering:pixelated">
   </div>`,
  `${OUT}/icon.png`,
);

// Banner: light store background, the coin emblem and the wordmark.
await render(
  `<div style="width:256px;height:128px;position:relative;overflow:hidden;border-radius:22px;
               background:url(/3ds/build/gen/bg-top-light.png) -40px -50px">
     <div style="position:absolute;left:14px;top:26px;width:76px;height:76px;border-radius:50%;
                 box-shadow:0 5px 10px rgba(44,62,69,.22), 0 0 0 3px #fff">
       <img src="/3ds/build/gen/emblem.png" style="width:76px;height:76px;display:block">
     </div>
     <div style="position:absolute;left:99px;top:40px;font:400 31px 'DM Sans';letter-spacing:-1.4px;color:#2c3e45;line-height:1">
       <b style="font-weight:800;color:#ff8a1f">forge.</b>store
     </div>
     <div style="position:absolute;left:101px;top:78px;font:800 9.5px 'Nunito';color:#5d727a;letter-spacing:.2px">
       ports para Nintendo 3DS
     </div>
   </div>`,
  `${OUT}/banner.png`,
);
await browser.close();

// Banner sound: the welcome chime, 3 s max, mono 16-bit (bannertool's limits).
execFileSync('ffmpeg', ['-v', 'error', '-y', '-i', 'assets/audio/intro-welcome.mp3', '-t', '2.9', '-ac', '1', '-ar', '32000', '-c:a', 'pcm_s16le', `${OUT}/banner.wav`]);
console.log('meta ->', OUT);
