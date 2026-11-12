// Renders the prototype's synthesized audio offline into raw PCM for NDSP:
// every interface sound, the opening arrangement and the repeating music.
// Output: <out>/sfx-<name>.pcm and <out>/music{,-start}.pcm (signed 16-bit stereo, 32000 Hz).
// Usage: node 3ds/tools/render-audio.mjs <out-dir>   (prototype server on :8642)
import { chromium } from 'playwright';
import { mkdirSync, writeFileSync } from 'node:fs';

const OUT = process.argv[2];
const BASE = process.env.FORGE_URL || 'http://localhost:8642';
mkdirSync(OUT, { recursive: true });

const browser = await chromium.launch({ args: ['--mute-audio'] });
const page = await browser.newPage();
await page.goto(`${BASE}/3ds/tools/render.html`, { waitUntil: 'networkidle' });

const result = await page.evaluate(async () => {
  const { renderAudio } = await import('/3ds/tools/audio-render.js');
  const files = await renderAudio();
  return Object.fromEntries(Object.entries(files).map(([name, pcm]) => [name, Array.from(pcm)]));
});
for (const [name, samples] of Object.entries(result)) {
  writeFileSync(`${OUT}/${name}`, Buffer.from(Int16Array.from(samples).buffer));
}
console.log(`Exported ${Object.keys(result).length} stereo PCM clips at 32000 Hz`);
await browser.close();
