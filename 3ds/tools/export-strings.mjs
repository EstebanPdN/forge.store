// Exports the prototype's interface strings (all 8 languages) to one JSON file
// for the native build. Usage: node 3ds/tools/export-strings.mjs <out.json>
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../', import.meta.url));
const src = readFileSync(`${root}src/js/core/i18n.js`, 'utf8');
const start = src.indexOf('const STRINGS = ') + 'const STRINGS = '.length;
const end = src.indexOf('Object.assign(STRINGS');
const strings = new Function(`return ${src.slice(start, end).trim().replace(/;$/, '')}`)();
for (const lang of ['pt', 'it', 'fr', 'de', 'ru', 'ja']) {
  strings[lang] = (await import(`${root}src/js/i18n/${lang}.js`)).default;
}
const native=JSON.parse(readFileSync(`${root}3ds/native-strings.json`,'utf8'));
for(const [lang,extra] of Object.entries(native))Object.assign(strings[lang],extra);
writeFileSync(process.argv[2], JSON.stringify(strings));
console.log('languages:', Object.keys(strings).join(' '), '· keys:', Object.keys(strings.es).length);
