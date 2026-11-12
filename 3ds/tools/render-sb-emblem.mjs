// Renders only the status-bar emblem (18 px, 1 px white ring) at 4x for pack.py --ui-only.
// Same markup as render-coins.mjs, without the prototype server: the icon is inlined.
// Usage: node 3ds/tools/render-sb-emblem.mjs <out-dir>   (3ds/build/gen/coins)
import { chromium } from 'playwright';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const OUT = process.argv[2];
const root = fileURLToPath(new URL('../../', import.meta.url));
const icon = readFileSync(`${root}assets/brand/forge-icon.png`).toString('base64');
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 64, height: 64 }, deviceScaleFactor: 4 });
await page.setContent(
  '<body style="margin:8px;background:transparent">' +
    '<div id="sb" style="width:18px;height:18px;box-sizing:border-box;border-radius:50%;border:1px solid #fff;overflow:hidden;background:#fff">' +
    `<img src="data:image/png;base64,${icon}" style="display:block;width:100%;height:100%"></div></body>`,
);
await page.evaluate(() => document.querySelector('#sb img').decode());
await page.locator('#sb').screenshot({ path: `${OUT}/sb-emblem.png`, omitBackground: true });
await browser.close();
console.log('sb-emblem: 18 px at 4x');
