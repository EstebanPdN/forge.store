// Maps the keyboard (Azahar's default layout plus friendly extras) and the
// console's on-screen buttons to 3DS buttons, with D-pad auto-repeat.

export const KEYMAP = {
  ArrowUp: 'UP',
  ArrowDown: 'DOWN',
  ArrowLeft: 'LEFT',
  ArrowRight: 'RIGHT',
  KeyT: 'UP',
  KeyG: 'DOWN',
  KeyF: 'LEFT',
  KeyH: 'RIGHT',
  KeyA: 'A',
  Enter: 'A',
  KeyS: 'B',
  Escape: 'B',
  Backspace: 'B',
  KeyZ: 'X',
  KeyX: 'Y',
  KeyQ: 'L',
  KeyW: 'R',
  KeyM: 'START',
  KeyN: 'SELECT',
  KeyB: 'HOME',
};

const REPEATS = new Set(['UP', 'DOWN', 'LEFT', 'RIGHT']);
const CIRCLE = { ArrowUp: [0, -6], ArrowDown: [0, 6], ArrowLeft: [-6, 0], ArrowRight: [6, 0] };

export class Input {
  constructor(device) {
    this.device = device;
    this.handler = null;
    this.textHandler = null;
    this.enabled = false;
    this.timers = new Map();
    this.circle = new Set();

    addEventListener('keydown', (e) => this.keydown(e));
    addEventListener('keyup', (e) => this.keyup(e));
    addEventListener('blur', () => this.releaseAll());

    device.addEventListener('pointerdown', (e) => {
      const el = e.target.closest('[data-btn]');
      if (!el) return;
      e.preventDefault();
      el.setPointerCapture?.(e.pointerId);
      const btn = el.dataset.btn;
      const release = () => {
        this.release(btn);
        el.removeEventListener('pointerup', release);
        el.removeEventListener('pointercancel', release);
      };
      el.addEventListener('pointerup', release);
      el.addEventListener('pointercancel', release);
      this.press(btn);
    });
  }

  keydown(e) {
    if (!this.enabled || e.defaultPrevented || e.target.closest?.('dialog')) return;
    // Enter activates a focused design-toolbar button. Other console shortcuts
    // must still work after closing the pixel inspector and restoring its focus.
    if (e.target.closest?.('.devbar') && (e.key === 'Enter' || e.key === ' ')) return;
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    if (e.target.closest?.('input, textarea, select')) return;
    const typing = this.textHandler && this.enabled;
    if (typing && !(e.code in CIRCLE) && e.key !== 'Escape') {
      if (e.key.length === 1 || e.key === 'Backspace' || e.key === 'Enter') {
        e.preventDefault();
        this.textHandler(e.key);
        return;
      }
    }
    const btn = KEYMAP[e.code];
    if (!btn) return;
    e.preventDefault();
    if (e.repeat) return;
    if (CIRCLE[e.code]) {
      this.circle.add(e.code);
      this.syncCircle();
    }
    this.press(btn);
  }

  keyup(e) {
    const btn = KEYMAP[e.code];
    if (!btn) return;
    if (CIRCLE[e.code]) {
      this.circle.delete(e.code);
      this.syncCircle();
    }
    this.release(btn);
  }

  syncCircle() {
    let x = 0;
    let y = 0;
    for (const code of this.circle) {
      x += CIRCLE[code][0];
      y += CIRCLE[code][1];
    }
    const pad = this.device.querySelector('.circle-pad');
    pad?.style.setProperty('--cpx', `${x}px`);
    pad?.style.setProperty('--cpy', `${y}px`);
  }

  press(btn) {
    this.device.querySelectorAll(`[data-btn="${btn}"]`).forEach((el) => el.classList.add('pressed'));
    if (this.timers.has(btn)) return;
    this.dispatch(btn, false);
    if (REPEATS.has(btn)) {
      const start = setTimeout(() => {
        const every = setInterval(() => this.dispatch(btn, true), 85);
        this.timers.set(btn, every);
      }, 330);
      this.timers.set(btn, start);
    } else {
      this.timers.set(btn, 0);
    }
  }

  release(btn) {
    this.device.querySelectorAll(`[data-btn="${btn}"]`).forEach((el) => el.classList.remove('pressed'));
    const timer = this.timers.get(btn);
    clearTimeout(timer);
    clearInterval(timer);
    this.timers.delete(btn);
  }

  releaseAll() {
    for (const btn of [...this.timers.keys()]) this.release(btn);
    this.circle.clear();
    this.syncCircle();
  }

  dispatch(btn, repeat) {
    if (!this.enabled || !this.handler) return;
    this.handler(btn, { repeat });
  }
}
