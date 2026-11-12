// Lightweight particle layer for banners and celebrations. Each preset is a small
// sprite system, easy to port to Citro2D (one textured quad per particle).

const rand = (a, b) => a + Math.random() * (b - a);
const TAU = Math.PI * 2;

const PRESETS = {
  sparkles: {
    rate: 7,
    max: 26,
    spawn: (w, h) => ({ x: rand(0, w), y: rand(18, h * 0.75), life: rand(1.2, 2.4), size: rand(1.2, 3.2), hue: rand(40, 55) }),
    draw(ctx, p, t) {
      const a = Math.sin((t / p.life) * Math.PI);
      star4(ctx, p.x, p.y, p.size * (0.6 + a * 0.6), `hsla(${p.hue}, 100%, 85%, ${a})`);
    },
  },
  leaves: {
    rate: 2.2,
    max: 14,
    spawn: (w, h) => ({ x: rand(-20, w * 0.8), y: -10, vx: rand(10, 24), vy: rand(12, 22), life: 12, size: rand(3, 5.5), rot: rand(0, TAU), spin: rand(-2, 2), hue: rand(85, 125) }),
    update(p, dt, t) {
      p.x += (p.vx + Math.sin(t * 2 + p.rot) * 10) * dt;
      p.y += p.vy * dt;
      p.rot += p.spin * dt;
    },
    draw(ctx, p) {
      ctx.save();
      ctx.translate(p.x, p.y);
      ctx.rotate(p.rot);
      ctx.fillStyle = `hsla(${p.hue}, 55%, 52%, .85)`;
      ctx.beginPath();
      ctx.ellipse(0, 0, p.size, p.size * 0.45, 0, 0, TAU);
      ctx.fill();
      ctx.restore();
    },
  },
  embers: {
    rate: 14,
    max: 60,
    blend: 'lighter',
    spawn: (w, h) => ({ x: rand(0, w), y: h + 4, vx: rand(-6, 6), vy: rand(-38, -18), life: rand(2, 4), size: rand(0.8, 2) }),
    update(p, dt, t) {
      p.x += (p.vx + Math.sin(t * 3 + p.size * 5) * 8) * dt;
      p.y += p.vy * dt;
    },
    draw(ctx, p, t) {
      const a = 1 - t / p.life;
      ctx.fillStyle = `rgba(255, ${140 + 80 * a | 0}, 60, ${a * 0.9})`;
      dot(ctx, p.x, p.y, p.size);
    },
  },
  stars: {
    rate: 30,
    max: 90,
    blend: 'lighter',
    spawn: (w, h) => {
      const ang = rand(0, TAU);
      return { ang, d: rand(2, 30), v: rand(14, 40), life: 5, cx: w / 2, cy: h * 0.45 };
    },
    update(p, dt) {
      p.v *= 1 + dt * 1.1;
      p.d += p.v * dt;
      p.x = p.cx + Math.cos(p.ang) * p.d * 1.5;
      p.y = p.cy + Math.sin(p.ang) * p.d;
    },
    draw(ctx, p) {
      const a = Math.min(1, p.d / 80);
      ctx.strokeStyle = `rgba(220, 235, 255, ${a})`;
      ctx.lineWidth = 0.6 + a;
      ctx.beginPath();
      ctx.moveTo(p.x, p.y);
      ctx.lineTo(p.x - Math.cos(p.ang) * p.v * 0.06 * 1.5, p.y - Math.sin(p.ang) * p.v * 0.06);
      ctx.stroke();
    },
  },
  speed: {
    rate: 18,
    max: 40,
    blend: 'lighter',
    spawn: (w, h) => ({ x: w + 30, y: rand(20, h - 10), vx: rand(-520, -300), life: 2, len: rand(20, 60) }),
    update(p, dt) {
      p.x += p.vx * dt;
    },
    draw(ctx, p) {
      const g = ctx.createLinearGradient(p.x, 0, p.x + p.len, 0);
      g.addColorStop(0, 'rgba(255,255,255,.65)');
      g.addColorStop(1, 'rgba(255,255,255,0)');
      ctx.strokeStyle = g;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(p.x, p.y);
      ctx.lineTo(p.x + p.len, p.y);
      ctx.stroke();
    },
  },
  sea: {
    rate: 6,
    max: 30,
    blend: 'lighter',
    spawn: (w, h) => ({ x: rand(0, w), y: rand(h * 0.55, h), life: rand(1, 2.2), size: rand(1, 2.2) }),
    draw(ctx, p, t) {
      const a = Math.sin((t / p.life) * Math.PI);
      star4(ctx, p.x, p.y, p.size * 1.6, `rgba(255, 245, 210, ${a * 0.9})`);
    },
  },
  twilight: {
    rate: 6,
    max: 34,
    spawn: (w, h) => ({ x: rand(0, w), y: h + 6, vy: rand(-22, -10), vx: rand(-4, 4), life: rand(4, 7), size: rand(2, 5), rot: rand(0, TAU) }),
    update(p, dt) {
      p.y += p.vy * dt;
      p.x += p.vx * dt;
      p.rot += dt * 0.6;
    },
    draw(ctx, p, t) {
      const a = Math.sin((t / p.life) * Math.PI);
      ctx.save();
      ctx.translate(p.x, p.y);
      ctx.rotate(p.rot);
      ctx.fillStyle = `rgba(10, 14, 20, ${a * 0.85})`;
      ctx.fillRect(-p.size / 2, -p.size / 2, p.size, p.size);
      ctx.strokeStyle = `rgba(94, 255, 214, ${a * 0.45})`;
      ctx.lineWidth = 0.6;
      ctx.strokeRect(-p.size / 2, -p.size / 2, p.size, p.size);
      ctx.restore();
    },
  },
  scan: {
    rate: 0.8,
    max: 3,
    blend: 'lighter',
    spawn: (w, h) => ({ y: -10, vy: rand(50, 90), life: 6 }),
    update(p, dt) {
      p.y += p.vy * dt;
    },
    draw(ctx, p, t, w) {
      const g = ctx.createLinearGradient(0, p.y - 14, 0, p.y);
      g.addColorStop(0, 'rgba(255,0,0,0)');
      g.addColorStop(1, 'rgba(255,40,40,.35)');
      ctx.fillStyle = g;
      ctx.fillRect(0, p.y - 14, w, 14);
    },
  },
  clouds: {
    rate: 0.35,
    max: 5,
    spawn: (w, h) => ({ x: -70, y: rand(20, h * 0.5), vx: rand(6, 12), life: 40, size: rand(16, 30) }),
    update(p, dt) {
      p.x += p.vx * dt;
    },
    draw(ctx, p) {
      ctx.fillStyle = 'rgba(255,255,255,.55)';
      for (const [dx, dy, r] of [[0, 0, 1], [0.9, -0.3, 0.75], [1.7, 0.05, 0.85], [-0.8, 0.15, 0.7]]) {
        dot(ctx, p.x + dx * p.size, p.y + dy * p.size, r * p.size * 0.7);
      }
    },
  },
  snow: {
    rate: 12,
    max: 60,
    spawn: (w) => ({ x: rand(0, w), y: -4, vy: rand(14, 30), life: 14, size: rand(0.7, 1.8), ph: rand(0, TAU) }),
    update(p, dt, t) {
      p.y += p.vy * dt;
      p.x += Math.sin(t + p.ph) * 8 * dt;
    },
    draw(ctx, p) {
      ctx.fillStyle = 'rgba(255,255,255,.85)';
      dot(ctx, p.x, p.y, p.size);
    },
  },
  hex: {
    rate: 1.4,
    max: 6,
    blend: 'lighter',
    spawn: (w, h) => ({ r: 4, life: 3.4, cx: w / 2, cy: h * 0.48, rot: rand(0, TAU) }),
    update(p, dt) {
      p.r += dt * 70;
      p.rot += dt * 0.5;
    },
    draw(ctx, p, t) {
      const a = 1 - t / p.life;
      ctx.strokeStyle = `rgba(255, 90, 210, ${a * 0.7})`;
      ctx.lineWidth = 2;
      ctx.beginPath();
      for (let i = 0; i <= 6; i++) {
        const ang = p.rot + (i * TAU) / 6;
        const x = p.cx + Math.cos(ang) * p.r;
        const y = p.cy + Math.sin(ang) * p.r;
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      }
      ctx.stroke();
    },
  },
  dust: {
    rate: 5,
    max: 30,
    blend: 'lighter',
    spawn: (w, h) => ({ x: rand(0, w), y: rand(0, h), vx: rand(-4, 4), vy: rand(-6, -1), life: rand(3, 6), size: rand(0.6, 1.5) }),
    update(p, dt) {
      p.x += p.vx * dt;
      p.y += p.vy * dt;
    },
    draw(ctx, p, t) {
      const a = Math.sin((t / p.life) * Math.PI);
      ctx.fillStyle = `rgba(255, 220, 140, ${a * 0.8})`;
      dot(ctx, p.x, p.y, p.size);
    },
  },
  pixels: {
    rate: 5,
    max: 28,
    spawn: (w, h) => ({ x: rand(0, w), y: h + 4, vy: rand(-26, -12), life: rand(3, 6), size: [2, 3, 4][(Math.random() * 3) | 0] }),
    update(p, dt) {
      p.y += p.vy * dt;
    },
    draw(ctx, p, t) {
      const a = Math.sin((t / p.life) * Math.PI) * 0.55;
      ctx.fillStyle = `rgba(255,255,255,${a})`;
      ctx.fillRect(Math.round(p.x), Math.round(p.y), p.size, p.size);
    },
  },
  petals: {
    rate: 3,
    max: 18,
    spawn: (w) => ({ x: rand(0, w), y: -6, vx: rand(-8, 8), vy: rand(14, 24), life: 14, size: rand(2.4, 4), rot: rand(0, TAU), spin: rand(-3, 3) }),
    update(p, dt, t) {
      p.x += (p.vx + Math.sin(t * 1.6 + p.rot) * 10) * dt;
      p.y += p.vy * dt;
      p.rot += p.spin * dt;
    },
    draw(ctx, p) {
      ctx.save();
      ctx.translate(p.x, p.y);
      ctx.rotate(p.rot);
      ctx.fillStyle = 'rgba(255, 170, 205, .85)';
      ctx.beginPath();
      ctx.ellipse(0, 0, p.size, p.size * 0.55, 0, 0, TAU);
      ctx.fill();
      ctx.restore();
    },
  },
};

// Burst used when an install finishes: forge sparks with gravity.
const SPARK = {
  update(p, dt) {
    p.vy += 260 * dt;
    p.vx *= 1 - dt * 0.8;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
  },
  draw(ctx, p, t) {
    const a = 1 - t / p.life;
    ctx.strokeStyle = `hsla(${p.hue}, 100%, ${60 + 30 * a}%, ${a})`;
    ctx.lineWidth = p.size;
    ctx.beginPath();
    ctx.moveTo(p.x, p.y);
    ctx.lineTo(p.x - p.vx * 0.025, p.y - p.vy * 0.025);
    ctx.stroke();
  },
};

function dot(ctx, x, y, r) {
  ctx.beginPath();
  ctx.arc(x, y, Math.max(0.3, r), 0, TAU);
  ctx.fill();
}
function star4(ctx, x, y, r, color) {
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.moveTo(x, y - r * 2);
  ctx.quadraticCurveTo(x, y, x + r * 2, y);
  ctx.quadraticCurveTo(x, y, x, y + r * 2);
  ctx.quadraticCurveTo(x, y, x - r * 2, y);
  ctx.quadraticCurveTo(x, y, x, y - r * 2);
  ctx.fill();
}

export class FX {
  constructor(canvas, width, height) {
    this.canvas = canvas;
    this.w = width;
    this.h = height;
    this.ctx = canvas.getContext('2d');
    this.particles = [];
    this.preset = null;
    this.acc = 0;
    this.running = false;
    this.resize(1);
  }

  /** Renders at the device zoom × devicePixelRatio for crisp particles. */
  resize(zoom) {
    const scale = Math.min(4, zoom * (window.devicePixelRatio || 1));
    this.scale = scale;
    this.canvas.width = Math.round(this.w * scale);
    this.canvas.height = Math.round(this.h * scale);
  }

  setPreset(name) {
    this.preset = PRESETS[name] || null;
    this.particles = this.particles.filter((p) => p.burst);
    this.start();
  }

  burst(x, y, count = 70) {
    for (let i = 0; i < count; i++) {
      const ang = rand(-Math.PI, 0) + rand(-0.4, 0.4);
      const v = rand(60, 220);
      this.particles.push({
        burst: true,
        x,
        y,
        vx: Math.cos(ang) * v,
        vy: Math.sin(ang) * v,
        life: rand(0.6, 1.4),
        size: rand(0.8, 1.8),
        hue: rand(20, 50),
        t: 0,
      });
    }
    this.start();
  }

  start() {
    if (this.running) return;
    this.running = true;
    this.last = performance.now();
    const loop = (now) => {
      if (!this.running) return;
      const dt = Math.min(0.05, (now - this.last) / 1000);
      this.last = now;
      this.step(dt);
      if (!this.preset && !this.particles.length) {
        this.running = false;
        this.clear();
        return;
      }
      requestAnimationFrame(loop);
    };
    requestAnimationFrame(loop);
  }

  stop() {
    this.running = false;
    this.particles = [];
    this.clear();
  }

  clear() {
    this.ctx.setTransform(1, 0, 0, 1, 0, 0);
    this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
  }

  step(dt) {
    const preset = this.preset;
    if (preset && !document.hidden) {
      this.acc += dt * preset.rate;
      const alive = this.particles.filter((p) => !p.burst).length;
      while (this.acc >= 1) {
        this.acc -= 1;
        if (alive < preset.max) this.particles.push({ ...preset.spawn(this.w, this.h), t: 0 });
      }
    }
    const ctx = this.ctx;
    this.clear();
    ctx.setTransform(this.scale, 0, 0, this.scale, 0, 0);
    this.particles = this.particles.filter((p) => {
      p.t += dt;
      if (p.t > p.life) return false;
      const sys = p.burst ? SPARK : preset;
      if (!sys) return false;
      sys.update?.(p, dt, p.t);
      ctx.globalCompositeOperation = (p.burst ? 'lighter' : sys.blend) || 'source-over';
      sys.draw(ctx, p, p.t, this.w, this.h);
      return true;
    });
    ctx.globalCompositeOperation = 'source-over';
  }
}
