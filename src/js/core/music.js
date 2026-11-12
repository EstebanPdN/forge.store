// Original background loop for the store: a relaxed bossa nova in C at 100 BPM
// (16 bars, electric piano, bass, shaker, rim and vibraphone melody).
// It is composed for this project; on 3DS it streams as stereo PCM through NDSP.

const BPM = 100;
const STEP = 60 / BPM / 4; // one sixteenth note
const BAR = 16;
const LOOP = 16 * BAR;

const CHORDS = {
  Fmaj9: { root: 41, notes: [57, 60, 64, 67] },
  Em9: { root: 40, notes: [55, 59, 62, 66] },
  A13: { root: 45, notes: [55, 59, 61, 66] },
  Dm9: { root: 38, notes: [53, 57, 60, 64] },
  G13: { root: 43, notes: [53, 57, 59, 64] },
  Cmaj9: { root: 48, notes: [52, 55, 59, 62] },
  Am7: { root: 45, notes: [55, 60, 64, 67] },
  D9: { root: 38, notes: [54, 57, 60, 64] },
  Fm6: { root: 41, notes: [56, 60, 62, 65] },
  A7b9: { root: 45, notes: [55, 58, 61, 64] },
};

const PROGRESSION = [
  ['Fmaj9'], ['Em9', 'A13'], ['Dm9'], ['G13'],
  ['Cmaj9'], ['Am7', 'D9'], ['Dm9', 'G13'], ['Cmaj9'],
  ['Fmaj9'], ['Fm6'], ['Em9'], ['A7b9'],
  ['Dm9'], ['G13'], ['Em9', 'A7b9'], ['Dm9', 'G13'],
];

// [step, length in steps, velocity] — a two-bar bossa comping figure.
const COMP = [
  [[0, 3, 0.9], [3, 2, 0.55], [6, 2, 0.75], [10, 3, 0.8], [13, 2, 0.55]],
  [[2, 2, 0.7], [6, 2, 0.8], [8, 3, 0.75], [11, 2, 0.6], [14, 2, 0.65]],
];

// [step, midi, length] per bar.
const MELODY = [
  [[2, 76, 4], [6, 74, 2], [8, 72, 4], [12, 69, 4]],
  [[0, 67, 3], [3, 71, 3], [6, 74, 2], [8, 73, 4], [12, 76, 4]],
  [[0, 77, 6], [6, 76, 2], [8, 74, 2], [10, 72, 2], [12, 69, 4]],
  [[2, 71, 6], [8, 69, 2], [10, 67, 6]],
  [[0, 76, 3], [3, 79, 3], [6, 81, 2], [8, 79, 4], [12, 76, 4]],
  [[0, 72, 4], [4, 76, 2], [8, 78, 4], [12, 76, 2], [14, 74, 2]],
  [[0, 77, 4], [4, 76, 2], [6, 74, 2], [8, 71, 2], [10, 74, 2], [12, 77, 2], [14, 81, 2]],
  [[0, 79, 6], [12, 76, 2], [14, 77, 2]],
  [[0, 81, 6], [6, 79, 2], [8, 76, 4], [12, 72, 4]],
  [[0, 80, 6], [6, 79, 2], [8, 77, 4], [12, 74, 4]],
  [[0, 76, 6], [6, 74, 2], [8, 71, 4], [12, 67, 4]],
  [[0, 73, 4], [4, 76, 2], [6, 79, 2], [8, 82, 4], [12, 81, 4]],
  [[2, 77, 6], [8, 76, 2], [10, 77, 2], [12, 81, 4]],
  [[0, 79, 4], [4, 77, 2], [6, 76, 2], [8, 74, 4], [12, 71, 4]],
  [[0, 71, 3], [3, 74, 3], [8, 73, 4], [12, 70, 2], [14, 69, 2]],
  [[0, 69, 4], [4, 72, 2], [6, 74, 2], [8, 77, 2], [10, 76, 2], [12, 74, 2], [14, 71, 2]],
];

const CLAVE = [0, 6, 12, 20, 26]; // bossa clave over two bars
const SHAKER = [0.9, 0.4, 0.62, 0.4];
const hz = (midi) => 440 * 2 ** ((midi - 69) / 12);

export class Music {
  constructor(ctx, out, reverb) {
    this.ctx = ctx;
    this.out = out;
    this.reverbSend = ctx.createGain();
    this.reverbSend.gain.value = 0.32;
    this.reverbSend.connect(reverb);
    this.running = false;
    this.step = 0;
    const noise = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate);
    const data = noise.getChannelData(0);
    for (let i = 0; i < data.length; i++) data[i] = Math.random() * 2 - 1;
    this.noise = noise;
  }

  start() {
    if (this.running) return;
    this.running = true;
    this.step = 0;
    this.next = this.ctx.currentTime + 0.12;
    this.timer = setInterval(() => this.schedule(), 25);
  }

  stop() {
    this.running = false;
    clearInterval(this.timer);
  }

  schedule() {
    while (this.next < this.ctx.currentTime + 0.16) {
      this.play(this.step, this.next);
      this.next += STEP;
      this.step++;
    }
  }

  play(globalStep, time) {
    const pass = Math.floor(globalStep / LOOP);
    const loopStep = globalStep % LOOP;
    const bar = Math.floor(loopStep / BAR);
    const s = loopStep % BAR;
    const names = PROGRESSION[bar];
    const chordAt = (step) => CHORDS[names[step < 8 || names.length === 1 ? 0 : 1]];
    const human = () => (Math.random() - 0.5) * 0.006;

    for (const [at, len, vel] of COMP[bar % 2]) {
      if (at === s) this.ep(time + human(), chordAt(s).notes, len * STEP, vel);
    }

    const bassHits = [
      [0, 5, chordAt(0).root],
      [6, 2, fifth(chordAt(0).root)],
      [8, 5, chordAt(8).root],
      [14, 2, fifth(chordAt(8).root)],
    ];
    for (const [at, len, midi] of bassHits) if (at === s) this.bass(time, midi, len * STEP);

    this.shaker(time + human(), SHAKER[s % 4]);
    if (CLAVE.includes((bar % 2) * BAR + s)) this.rim(time);
    if (s === 0) this.kick(time, 0.9);
    if (s === 8) this.kick(time, 0.65);

    if (pass >= 1) {
      for (const [at, midi, len] of MELODY[bar]) if (at === s) this.vibe(time, midi, len * STEP);
    }
  }

  env(gainNode, time, peak, hold, release) {
    const g = gainNode.gain;
    g.setValueAtTime(0.0001, time);
    g.exponentialRampToValueAtTime(peak, time + 0.006);
    g.exponentialRampToValueAtTime(peak * 0.45, time + Math.min(0.35, hold));
    g.setValueAtTime(peak * 0.45, time + hold);
    g.exponentialRampToValueAtTime(0.0001, time + hold + release);
  }

  ep(time, notes, dur, vel) {
    const ctx = this.ctx;
    const filter = ctx.createBiquadFilter();
    filter.type = 'lowpass';
    filter.frequency.value = 2400;
    filter.connect(this.out);
    filter.connect(this.reverbSend);
    for (const midi of notes) {
      const f = hz(midi);
      const amp = ctx.createGain();
      this.env(amp, time, 0.032 * vel, dur, 0.55);
      amp.connect(filter);
      const parts = [
        ['sine', f, 1],
        ['triangle', f * 1.003, 0.35],
      ];
      for (const [type, freq, level] of parts) {
        const osc = ctx.createOscillator();
        const g = ctx.createGain();
        osc.type = type;
        osc.frequency.value = freq;
        g.gain.value = level;
        osc.connect(g).connect(amp);
        osc.start(time);
        osc.stop(time + dur + 0.7);
      }
      // Short bell-like attack, the "tine" of an electric piano.
      const tine = ctx.createOscillator();
      const tg = ctx.createGain();
      tine.frequency.value = f * 4;
      tg.gain.setValueAtTime(0.0001, time);
      tg.gain.exponentialRampToValueAtTime(0.006 * vel, time + 0.003);
      tg.gain.exponentialRampToValueAtTime(0.0001, time + 0.12);
      tine.connect(tg).connect(filter);
      tine.start(time);
      tine.stop(time + 0.15);
    }
  }

  bass(time, midi, dur) {
    const ctx = this.ctx;
    const f = hz(midi);
    const filter = ctx.createBiquadFilter();
    filter.type = 'lowpass';
    filter.frequency.value = 650;
    const amp = ctx.createGain();
    amp.gain.setValueAtTime(0.0001, time);
    amp.gain.exponentialRampToValueAtTime(0.16, time + 0.012);
    amp.gain.exponentialRampToValueAtTime(0.09, time + 0.22);
    amp.gain.setValueAtTime(0.09, time + dur * 0.9);
    amp.gain.exponentialRampToValueAtTime(0.0001, time + dur + 0.08);
    filter.connect(amp).connect(this.out);
    for (const type of ['triangle', 'sine']) {
      const osc = ctx.createOscillator();
      osc.type = type;
      osc.frequency.value = f;
      osc.connect(filter);
      osc.start(time);
      osc.stop(time + dur + 0.12);
    }
  }

  shaker(time, vel) {
    const ctx = this.ctx;
    const src = ctx.createBufferSource();
    src.buffer = this.noise;
    const bp = ctx.createBiquadFilter();
    bp.type = 'bandpass';
    bp.frequency.value = 7200;
    bp.Q.value = 1.1;
    const amp = ctx.createGain();
    amp.gain.setValueAtTime(0.0001, time);
    amp.gain.exponentialRampToValueAtTime(0.022 * vel, time + 0.004);
    amp.gain.exponentialRampToValueAtTime(0.0001, time + 0.055);
    src.connect(bp).connect(amp).connect(this.out);
    src.start(time, Math.random() * 0.8);
    src.stop(time + 0.07);
  }

  rim(time) {
    const ctx = this.ctx;
    const amp = ctx.createGain();
    amp.gain.setValueAtTime(0.0001, time);
    amp.gain.exponentialRampToValueAtTime(0.05, time + 0.002);
    amp.gain.exponentialRampToValueAtTime(0.0001, time + 0.06);
    amp.connect(this.out);
    amp.connect(this.reverbSend);
    for (const [type, f] of [['sine', 1650], ['triangle', 820]]) {
      const osc = ctx.createOscillator();
      osc.type = type;
      osc.frequency.value = f;
      osc.connect(amp);
      osc.start(time);
      osc.stop(time + 0.07);
    }
  }

  kick(time, vel) {
    const ctx = this.ctx;
    const osc = ctx.createOscillator();
    const amp = ctx.createGain();
    osc.frequency.setValueAtTime(115, time);
    osc.frequency.exponentialRampToValueAtTime(46, time + 0.14);
    amp.gain.setValueAtTime(0.0001, time);
    amp.gain.exponentialRampToValueAtTime(0.32 * vel, time + 0.005);
    amp.gain.exponentialRampToValueAtTime(0.0001, time + 0.3);
    osc.connect(amp).connect(this.out);
    osc.start(time);
    osc.stop(time + 0.32);
  }

  vibe(time, midi, dur) {
    const ctx = this.ctx;
    const f = hz(midi);
    const amp = ctx.createGain();
    const length = dur + 0.9;
    amp.gain.setValueAtTime(0.0001, time);
    amp.gain.exponentialRampToValueAtTime(0.05, time + 0.004);
    amp.gain.exponentialRampToValueAtTime(0.0001, time + length);
    const trem = ctx.createGain();
    trem.gain.value = 0.75;
    const lfo = ctx.createOscillator();
    const depth = ctx.createGain();
    lfo.frequency.value = 5.6;
    depth.gain.value = 0.25;
    lfo.connect(depth).connect(trem.gain);
    amp.connect(trem);
    trem.connect(this.out);
    trem.connect(this.reverbSend);
    for (const [ratio, level] of [[1, 1], [4, 0.16], [10.2, 0.03]]) {
      const osc = ctx.createOscillator();
      const g = ctx.createGain();
      osc.frequency.value = f * ratio;
      g.gain.value = level;
      osc.connect(g).connect(amp);
      osc.start(time);
      osc.stop(time + length);
    }
    lfo.start(time);
    lfo.stop(time + length);
  }
}

function fifth(root) {
  return root + 7 > 52 ? root - 5 : root + 7;
}
