// Offline V2 font/UI-icon pipeline. No server or browser is started.
// Usage: node 3ds/tools/render-fonts.mjs [project-root]
// Dependencies: tools/.venv with font-requirements.txt; system Cairo for SVG icons.
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
const ROOT = resolve(process.argv[2] || fileURLToPath(new URL('../../', import.meta.url)));
const GEN = `${ROOT}/3ds/build/gen`;
mkdirSync(GEN, { recursive: true });
function run(command, args, options = {}) {
  const r = spawnSync(command, args, { stdio: 'inherit', ...options });
  if (r.error) throw r.error;
  if (r.status !== 0) process.exit(r.status || 1);
}
// Refresh all eight languages from source without mutating the packaged strings.
run(process.execPath, [`${ROOT}/3ds/tools/export-strings.mjs`, `${GEN}/font-strings.json`]);
// Read the same local SVG definitions used on the web. The object is data, no DOM needed.
const source = readFileSync(`${ROOT}/src/js/core/icons.js`, 'utf8');
const literal = source.match(/const ui = (\{[\s\S]*?\n\});/);
if (!literal) throw new Error('UI icon object not found');
writeFileSync(`${GEN}/font-ui-icons.json`, JSON.stringify(new Function(`return (${literal[1]})`)()));
run(process.env.FORGE_PYTHON || `${ROOT}/3ds/tools/.venv/bin/python`, ['-I', `${ROOT}/3ds/tools/render-fonts.py`, ROOT], {
  env: { ...process.env, DYLD_FALLBACK_LIBRARY_PATH: process.env.DYLD_FALLBACK_LIBRARY_PATH || '/opt/homebrew/lib:/usr/local/lib:/usr/lib' },
});
