// Synthesized interface sounds (Web Audio), the site's intro MP3s and the music bus.
// On 3DS these become short PCM samples played through NDSP channels.
import { Music } from './music.js';

const NOTE = (midi) => 440 * 2 ** ((midi - 69) / 12);
export const MUSIC_LEVEL = 1.7; // Twice the previous 0.85 music level.

export class AudioEngine {
  constructor() {
    this.ctx = null;
    this.volume = 0.8;
    this.musicOn = true;
    this.sfxOn = true;
    this.files = new Map();
  }

  /** Must run inside a user gesture (the power-on click). */
  unlock() {
    if (!this.ctx) {
      const ctx = new (window.AudioContext || window.webkitAudioContext)();
      this.createGraph(ctx);
    }
    this.ctx.resume();
  }

  /** Shared by live playback and the native PCM exporter. */
  createGraph(ctx) {
    this.ctx = ctx;
    this.master = ctx.createGain();
    this.master.gain.value = this.volume;
    this.master.connect(ctx.destination);

    this.reverb = ctx.createConvolver();
    this.reverb.buffer = this.impulse(1.8, 2.6);
    this.reverbOut = ctx.createGain();
    this.reverbOut.gain.value = 0.9;
    this.reverb.connect(this.reverbOut).connect(this.master);

    this.sfxBus = ctx.createGain();
    this.sfxBus.gain.value = 0.9;
    this.sfxBus.connect(this.master);
    this.sfxSend = ctx.createGain();
    this.sfxSend.gain.value = 0.18;
    this.sfxBus.connect(this.sfxSend).connect(this.reverb);

    this.musicFilter = ctx.createBiquadFilter();
    this.musicFilter.type = 'lowpass';
    // Web Audio expresses lowpass Q in dB; NDSP uses the linear value .707.
    this.musicFilter.Q.value = 20 * Math.log10(Math.SQRT1_2);
    this.musicFilter.frequency.value = 18000;
    this.musicBus = ctx.createGain();
    this.musicBus.gain.value = 0;
    this.musicFilter.connect(this.musicBus).connect(this.master);
    // Keep the music's reverb inside its filter/fade/duck bus. Sharing the
    // effects return left a wet copy audible after the music was muted.
    this.musicReverb = ctx.createConvolver();
    this.musicReverb.buffer = this.reverb.buffer;
    this.musicReverbOut = ctx.createGain();
    this.musicReverbOut.gain.value = 0.9;
    this.musicReverb.connect(this.musicReverbOut).connect(this.musicFilter);
    this.music = new Music(ctx, this.musicFilter, this.musicReverb);
    this.noiseBuffer = this.makeNoise();
  }

  impulse(seconds, decay) {
    const rate = this.ctx.sampleRate;
    const length = Math.floor(rate * seconds);
    const buffer = this.ctx.createBuffer(2, length, rate);
    for (let c = 0; c < 2; c++) {
      const data = buffer.getChannelData(c);
      for (let i = 0; i < length; i++) data[i] = (Math.random() * 2 - 1) * (1 - i / length) ** decay;
    }
    return buffer;
  }

  makeNoise() {
    const buffer = this.ctx.createBuffer(1, this.ctx.sampleRate, this.ctx.sampleRate);
    const data = buffer.getChannelData(0);
    for (let i = 0; i < data.length; i++) data[i] = Math.random() * 2 - 1;
    return buffer;
  }

  setVolume(v) {
    this.volume = v;
    if (this.ctx) this.master.gain.setTargetAtTime(v, this.ctx.currentTime, 0.05);
    // Preloaded files have no playback gain yet. NaN here used to abort the
    // settings notification before the volume display could update.
    for (const el of this.files.values())
      if (Number.isFinite(el.baseVolume)) el.volume = Math.min(1, el.baseVolume * v * 1.25);
  }

  setMusic(on) {
    this.musicOn = on;
    if (!this.ctx) return;
    if (on && this.musicWanted) this.startMusic();
    else this.stopMusic(true);
  }

  setSfx(on) {
    this.sfxOn = on;
  }

  /** Starts the store loop with a fade-in. */
  startMusic(fade = 2.5) {
    this.musicWanted = true;
    if (!this.ctx || !this.musicOn) return;
    clearTimeout(this.stopTimer);
    this.music.start();
    const g = this.musicBus.gain;
    g.cancelScheduledValues(this.ctx.currentTime);
    g.setValueAtTime(g.value, this.ctx.currentTime);
    g.linearRampToValueAtTime(this.ducked ? 0 : MUSIC_LEVEL, this.ctx.currentTime + fade);
  }

  /** Silences the music while a trailer with sound plays, and brings it back after. */
  duck(on) {
    this.ducked = on;
    if (!this.ctx) return;
    const g = this.musicBus.gain;
    g.cancelScheduledValues(this.ctx.currentTime);
    g.setTargetAtTime(on || !this.musicWanted || !this.musicOn ? 0 : MUSIC_LEVEL, this.ctx.currentTime, 0.18);
  }

  stopMusic(keepWanted = false, fade = 0.6) {
    if (!keepWanted) this.musicWanted = false;
    if (!this.ctx) return;
    const g = this.musicBus.gain;
    g.cancelScheduledValues(this.ctx.currentTime);
    g.setValueAtTime(g.value, this.ctx.currentTime);
    g.linearRampToValueAtTime(0, this.ctx.currentTime + fade);
    clearTimeout(this.stopTimer);
    this.stopTimer = setTimeout(() => {
      if (!this.musicWanted || !this.musicOn) this.music.stop();
    }, fade * 1000 + 100);
  }

  /** Muffles the music (detail pages, installs) — 0 is open, 1 is very muffled. */
  muffle(amount) {
    if (!this.ctx) return;
    const freq = 18000 * (1 - amount) ** 3 + 900 * amount;
    this.musicFilter.frequency.setTargetAtTime(freq, this.ctx.currentTime, 0.25);
  }

  /** Plays one of the intro MP3s through an <audio> element, like the site does. */
  playFile(url, baseVolume = 0.5) {
    if (!this.sfxOn) return null;
    let el = this.files.get(url);
    if (!el) {
      el = new window.Audio(url);
      el.preload = 'auto';
      this.files.set(url, el);
    }
    el.baseVolume = baseVolume;
    el.volume = Math.min(1, baseVolume * this.volume * 1.25);
    el.currentTime = 0;
    el.play().catch(() => {});
    return el;
  }

  stopFile(url) {
    const el = this.files.get(url);
    if (el) {
      el.pause();
      el.currentTime = 0;
    }
  }

  preload(urls) {
    for (const url of urls) {
      if (this.files.has(url)) continue;
      const el = new window.Audio(url);
      el.preload = 'auto';
      this.files.set(url, el);
    }
  }

  // --- synthesis helpers -------------------------------------------------

  tone({ type = 'sine', freq = 880, to, dur = 0.1, gain = 0.06, delay = 0, attack = 0.004, bus }) {
    const ctx = this.ctx;
    const t = ctx.currentTime + delay;
    const osc = ctx.createOscillator();
    const env = ctx.createGain();
    osc.type = type;
    osc.frequency.setValueAtTime(freq, t);
    if (to) osc.frequency.exponentialRampToValueAtTime(to, t + dur);
    env.gain.setValueAtTime(0.0001, t);
    env.gain.exponentialRampToValueAtTime(gain, t + attack);
    env.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    osc.connect(env).connect(bus || this.sfxBus);
    osc.start(t);
    osc.stop(t + dur + 0.05);
  }

  noise({ dur = 0.1, gain = 0.05, delay = 0, type = 'bandpass', freq = 2000, to, q = 1 }) {
    const ctx = this.ctx;
    const t = ctx.currentTime + delay;
    const src = ctx.createBufferSource();
    src.buffer = this.noiseBuffer;
    const filter = ctx.createBiquadFilter();
    filter.type = type;
    filter.Q.value = q;
    filter.frequency.setValueAtTime(freq, t);
    if (to) filter.frequency.exponentialRampToValueAtTime(to, t + dur);
    const env = ctx.createGain();
    env.gain.setValueAtTime(0.0001, t);
    env.gain.exponentialRampToValueAtTime(gain, t + Math.min(0.02, dur / 3));
    env.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    src.connect(filter).connect(env).connect(this.sfxBus);
    src.start(t, Math.random() * 0.5);
    src.stop(t + dur + 0.05);
  }

  bell(freq, { delay = 0, gain = 0.05, dur = 1.2 } = {}) {
    this.tone({ freq, dur, gain, delay, attack: 0.003 });
    this.tone({ freq: freq * 2.76, dur: dur * 0.35, gain: gain * 0.35, delay, attack: 0.002 });
    this.tone({ freq: freq * 5.4, dur: dur * 0.15, gain: gain * 0.15, delay, attack: 0.002 });
  }

  /** Named interface sounds. */
  sfx(name, opts = {}) {
    if (!this.ctx || !this.sfxOn) return;
    const p = 2 ** ((opts.pitch || 0) / 12);
    switch (name) {
      case 'move':
        this.tone({ freq: 1180 * p, to: 1080 * p, dur: 0.05, gain: 0.045 });
        break;
      case 'select':
        this.tone({ type: 'triangle', freq: 660, dur: 0.07, gain: 0.07 });
        this.tone({ type: 'triangle', freq: 990, dur: 0.11, gain: 0.07, delay: 0.05 });
        break;
      case 'back':
        this.tone({ type: 'triangle', freq: 840, to: 560, dur: 0.11, gain: 0.06 });
        break;
      case 'open':
        this.noise({ freq: 500, to: 3200, dur: 0.24, gain: 0.035, q: 0.8 });
        this.tone({ freq: 1320, dur: 0.22, gain: 0.04, delay: 0.07 });
        this.tone({ freq: 1760, dur: 0.26, gain: 0.03, delay: 0.12 });
        break;
      case 'close':
        this.noise({ freq: 3000, to: 500, dur: 0.2, gain: 0.03, q: 0.8 });
        this.tone({ type: 'triangle', freq: 990, to: 660, dur: 0.14, gain: 0.045 });
        break;
      case 'tab':
        this.tone({ freq: 1480, dur: 0.04, gain: 0.045 });
        this.tone({ freq: 1760, dur: 0.05, gain: 0.04, delay: 0.045 });
        break;
      case 'page':
        this.noise({ freq: 900, to: 2600, dur: 0.17, gain: 0.04, q: 1.2 });
        break;
      case 'error':
        this.tone({ type: 'square', freq: 233, dur: 0.09, gain: 0.03 });
        this.tone({ type: 'square', freq: 196, dur: 0.15, gain: 0.03, delay: 0.11 });
        break;
      case 'type':
        this.noise({ type: 'highpass', freq: 3500, dur: 0.03, gain: 0.05 });
        this.tone({ freq: 1900 * p, dur: 0.025, gain: 0.02 });
        break;
      case 'toggle':
        this.tone({ freq: 880, dur: 0.04, gain: 0.05 });
        this.tone({ freq: 1320, dur: 0.06, gain: 0.05, delay: 0.04 });
        break;
      case 'download':
        [1046.5, 1318.5, 1568, 2093].forEach((f, i) =>
          this.tone({ type: 'triangle', freq: f, dur: 0.14, gain: 0.05, delay: i * 0.065 }),
        );
        break;
      case 'complete':
        [523.25, 659.25, 783.99, 987.77, 1318.5].forEach((f, i) => this.bell(f, { delay: i * 0.045, gain: 0.045 }));
        break;
      case 'anvil': {
        const base = 520;
        [1, 2.76, 5.4, 8.93].forEach((r, i) =>
          this.tone({ freq: base * r, dur: 0.7 / (i + 1), gain: 0.07 / (i + 1), attack: 0.001 }),
        );
        this.noise({ type: 'highpass', freq: 4000, dur: 0.06, gain: 0.06 });
        break;
      }
      case 'warn':
        this.tone({ type: 'triangle', freq: 740, dur: 0.1, gain: 0.05 });
        this.tone({ type: 'triangle', freq: 740, dur: 0.12, gain: 0.05, delay: 0.14 });
        break;
      case 'launch':
        this.noise({ freq: 300, to: 5000, dur: 0.6, gain: 0.04, q: 0.7 });
        [NOTE(72), NOTE(76), NOTE(79), NOTE(84)].forEach((f, i) => this.bell(f, { delay: 0.25 + i * 0.06, gain: 0.04 }));
        break;
      case 'power':
        this.tone({ freq: 220, to: 440, dur: 0.25, gain: 0.04 });
        break;
      default:
        break;
    }
  }
}
