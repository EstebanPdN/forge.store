// Run runAudioChecks() in the Code Atlas preview. Offline renders exercise the
// real bus controls; they detect reverb bypasses that gain-value checks miss.
import { AudioEngine, MUSIC_LEVEL } from '../../src/js/core/audio.js';

function rms(buffer, start, end) {
  const data = buffer.getChannelData(0);
  const a = Math.round(start * buffer.sampleRate), b = Math.round(end * buffer.sampleRate);
  let sum = 0;
  for (let i = a; i < b; i++) sum += data[i] * data[i];
  return Math.sqrt(sum / (b - a));
}

async function render(change, restore) {
  const ctx = new OfflineAudioContext(2, 48000 * 5, 48000);
  const e = new AudioEngine();
  e.createGraph(ctx);
  e.musicWanted = true;
  e.musicBus.gain.value = MUSIC_LEVEL;
  for (let s = 0; s < 33; s++) e.music.play(s, .05 + s * .15);
  const first = ctx.suspend(1).then(() => { change(e); return ctx.resume(); });
  const second = restore && ctx.suspend(3).then(() => { restore(e); return ctx.resume(); });
  const b = await ctx.startRendering();
  await first;
  if (second) await second;
  clearTimeout(e.stopTimer);
  return { before: rms(b, .3, .9), after: rms(b, 2.5, 2.9), restored: rms(b, 4, 4.8) };
}

export async function runAudioChecks() {
  const preloaded = new AudioEngine();
  preloaded.preload(['assets/audio/intro-welcome.mp3', 'assets/audio/intro-enter.mp3']);
  preloaded.setVolume(.4); // Must not assign NaN to an unplayed HTMLAudioElement.
  const mute = await render(e => e.setMusic(false));
  const duck = await render(e => e.duck(true), e => e.duck(false));
  const volume = await render(e => e.setVolume(0), e => e.setVolume(.8));
  const ok = mute.before > .01 && mute.after === 0 && mute.restored === 0 &&
    duck.after < duck.before / 1000 && duck.restored > .01 &&
    volume.after < 1e-10 && volume.restored > .01;
  if (!ok) throw new Error(JSON.stringify({ mute, duck, volume }));
  return { ok, preloadedVolume: preloaded.volume, mute, duck, volume };
}
