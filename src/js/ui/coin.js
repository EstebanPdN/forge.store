// 3D coin: a face with real thickness built from stacked discs, a specular sweep
// and a gentle side-to-side wobble. On 3DS it maps to a small Citro3D cylinder
// (or a pre-rendered sprite loop) whose depth pops out with the 3D slider.
import { h } from '../core/dom.js';

function mix(a, b, t) {
  const pa = a.match(/\w\w/g).map((x) => parseInt(x, 16));
  const pb = b.match(/\w\w/g).map((x) => parseInt(x, 16));
  return '#' + pa.map((v, i) => Math.round(v + (pb[i] - v) * t).toString(16).padStart(2, '0')).join('');
}

/**
 * coin({ face, back, size, depth, edge: [light, dark], cls })
 * `face` is the element shown on the front; `back` defaults to a copy of it.
 */
export function coin({ face, back, size = 74, depth = 8, edge = ['#ffffff', '#c9d2d7'], cls = '' }) {
  const layers = [];
  const count = Math.max(7, Math.round(depth * 1.6));
  for (let i = 0; i < count; i++) {
    const z = -depth / 2 + (depth * i) / (count - 1);
    // Lighter in the middle of the rim, darker towards both faces: a rounded edge.
    const shade = Math.abs(i / (count - 1) - 0.5) * 2;
    layers.push(h('i.coin-edge', { style: { transform: `translateZ(${z.toFixed(2)}px)`, background: mix(edge[0], edge[1], shade) } }));
  }
  const front = h('div.coin-face.front', { style: { transform: `translateZ(${depth / 2 + 0.2}px)` } }, face, h('i.coin-shine'));
  const backFace = h('div.coin-face.back', { style: { transform: `rotateY(180deg) translateZ(${depth / 2 + 0.2}px)` } }, back || face.cloneNode(true));
  return h(
    'div.coin' + (cls ? '.' + cls : ''),
    { style: { '--size': `${size}px` } },
    h('div.coin-body', layers, backFace, front),
    h('i.coin-shadow'),
  );
}
