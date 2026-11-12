// Browser-side renderer, shared by the export CLI and the Code Atlas preview.
import { AudioEngine } from '../../src/js/core/audio.js';

export const RATE = 32000;
export const EFFECTS = ['move', 'select', 'back', 'open', 'close', 'tab', 'page', 'error', 'type', 'toggle', 'download', 'complete', 'anvil', 'warn', 'launch'];

function engine(seconds) {
  const ctx = new OfflineAudioContext(2, Math.ceil(seconds * RATE), RATE);
  const e = new AudioEngine();
  e.volume = 1; // Master/fade/duck are applied live by NDSP, exactly once.
  e.createGraph(ctx);
  // The native lowpass runs in real time. Export the unfiltered mix.
  e.musicFilter.disconnect();
  e.musicBus.disconnect();
  e.music.out = e.master;
  e.musicReverbOut.disconnect();
  e.musicReverbOut.connect(e.master);
  return e;
}

function pcm(buffer, from = 0, to = buffer.length, trim = false) {
  const left = buffer.getChannelData(0), right = buffer.getChannelData(1);
  if (trim) while (to > from + 64 && Math.max(Math.abs(left[to - 1]), Math.abs(right[to - 1])) < 8 / 32767) to--;
  const out = new Int16Array((to - from) * 2);
  for (let i = from; i < to; i++) {
    out[(i - from) * 2] = Math.max(-32768, Math.min(32767, Math.round(left[i] * 32767)));
    out[(i - from) * 2 + 1] = Math.max(-32768, Math.min(32767, Math.round(right[i] * 32767)));
  }
  return out;
}

export async function renderAudio() {
  const files = {};
  for (const name of EFFECTS) {
    const e = engine(2.2);
    e.sfx(name);
    files[`sfx-${name}.pcm`] = pcm(await e.ctx.startRendering(), 0, e.ctx.length, true);
  }
  const stepSeconds = 60 / 100 / 4, steps = 256, lead = .12;
  const e = engine(lead + 3 * steps * stepSeconds + 2);
  for (let step = 0; step < steps * 3; step++) e.music.play(step, lead + step * stepSeconds);
  const rendered = await e.ctx.startRendering();
  const loopStart = Math.round((lead + 2 * steps * stepSeconds) * RATE);
  const loopEnd = Math.round((lead + 3 * steps * stepSeconds) * RATE);
  // Preserve the quiet first pass and the arrival of the vibraphone. The second
  // pass also establishes its reverb before the repeating section begins.
  files['music-start.pcm'] = pcm(rendered, 0, loopStart);
  const loop = pcm(rendered, loopStart, loopEnd);
  const before = pcm(rendered, loopStart - 256, loopStart);
  for (let i = 0; i < before.length; i++) {
    const at = loop.length - before.length + i, a = Math.floor(i / 2) / 255;
    loop[at] = Math.round(loop[at] * (1 - a) + before[i] * a);
  }
  files['music.pcm'] = loop;
  return files;
}
