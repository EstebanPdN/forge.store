// Pixel geometry mirrors update_icon.h: one anvil/hammer family morphs into a check.
// Canvas coverage is binary at 40×40; CSS never rotates or filters the bitmap.
const mix = (a, b, t) => a + (b - a) * t;
const point = (a, b, t) => [mix(a[0], b[0], t), mix(a[1], b[1], t)];
const ease = (t) => { t = Math.max(0, Math.min(1, t)); return t * t * (3 - 2 * t); };
const anvil = [[4,24],[34,24],[7,26],[34,26],[16,28],[28,28],[17,30],[27,30],[17,32],[27,32],[13,34],[32,34],[12,36],[33,36],[33,36],[33,36],[33,36]];
// Fill a T-shaped head perpendicular to the handle's 2:1 axis.
const head = Array.from({ length: 6 }, (_, i) => {
  const along = -2 + 2 * Math.floor(i / 2), across = i % 2 ? 6 : -6;
  return [17 + along * .89442719 - across * .44721360, 11 + along * .44721360 + across * .89442719];
});
// Compact 18px anvil for the console row. Integer runs stay legible on the LCD;
// this is drawn separately instead of shrinking the animated 40px hammer.
export function forgeMark(label) {
  const el = document.createElement('span');
  el.className = 'forge-mode-mark'; el.title = label; el.setAttribute('aria-label', label);
  const runs = [[14,2,3],[14,3,3],[1,4,16],[1,5,16],[2,6,14],[3,7,12],[6,8,7],[8,9,4],[8,10,4],[7,11,6],[7,12,6],[5,13,10],[4,14,12],[4,15,12]];
  el.innerHTML = `<svg width="18" height="18" viewBox="0 0 18 18" fill="currentColor" shape-rendering="crispEdges" aria-hidden="true">${runs.map(([x,y,w]) => `<rect x="${x}" y="${y}" width="${w}" height="1"/>`).join('')}</svg>`;
  return el;
}
function shape(kind, age) {
  const check = kind === 'enabled', cycle = Math.max(0, age) / (kind === 'checking' ? .72 : 1.8) % 1;
  const swing = cycle < .5 ? 1 : cycle < .72 ? 1 - ease((cycle - .5) / .22) : ease((cycle - .72) / .28);
  const rotation = -.35 + .40 * swing;
  return { green: check ? 1 : 0, parts: [0,1,2].map((part) => Array.from({ length: 17 }, (_, i) => {
    const t = i / 16, all = (part + t) / 3;
    if (check) return all < 1 / 3 ? point([8,19], [16,27], all * 3) : point([16,27], [32,11], (all - 1 / 3) * 1.5);
    if (!part) return anvil[i];
    const p = part === 1 ? point([17,11], [31,18], t) : head[Math.min(i, 5)], x = p[0] - 31, y = p[1] - 18;
    return [31 + x * Math.cos(rotation) - y * Math.sin(rotation), 18 + x * Math.sin(rotation) + y * Math.cos(rotation)];
  })) };
}
export class ForgeSeal {
  constructor(state) {
    this.el = document.createElement('canvas'); this.el.width = this.el.height = 40;
    this.el.setAttribute('aria-hidden', 'true'); this.context = this.el.getContext('2d');
    this.kind = state; this.changed = performance.now() / 1000;
  }
  frame(now) {
    const next = shape(this.kind, now - this.changed), t = ease((now - this.changed) / .48);
    if (!this.from || t >= 1) return next;
    return { green: mix(this.from.green, next.green, t), parts: next.parts.map((p, j) => p.map((q, i) => point(this.from.parts[j][i], q, t))) };
  }
  set(state) {
    if (state === this.kind) return;
    const now = performance.now() / 1000; this.from = this.frame(now); this.kind = state; this.changed = now;
  }
  draw = () => {
    const frame = this.frame(performance.now() / 1000), pixels = new Uint8Array(1600);
    for (const part of frame.parts) for (let i = 1; i < part.length; i++) {
      const [ax, ay] = part[i - 1].map(Math.round), [bx, by] = part[i].map(Math.round);
      const dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy;
      for (let y = Math.min(ay, by) - 2; y <= Math.max(ay, by) + 2; y++) for (let x = Math.min(ax, bx) - 2; x <= Math.max(ax, bx) + 2; x++) {
        if (x < 0 || x >= 40 || y < 0 || y >= 40) continue;
        const t = length ? Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / length)) : 0;
        if ((x - ax - t * dx) ** 2 + (y - ay - t * dy) ** 2 <= 2.25) pixels[y * 40 + x] = 1;
      }
    }
    const rgba = this.context.createImageData(40, 40), green = frame.green;
    for (let i = 0; i < pixels.length; i++) if (pixels[i]) rgba.data.set([mix(255,79,green), mix(138,158,green), mix(31,134,green),255], i * 4);
    this.context.putImageData(rgba, 0, 0); this.raf = requestAnimationFrame(this.draw);
  };
  start() { if (!this.raf) this.draw(); }
  stop() { cancelAnimationFrame(this.raf); this.raf = null; }
}
