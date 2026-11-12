// Keep decoded local banner images alive across hover changes and scene visits.
// The browser owns its HTTP cache; this bounded working set avoids repeat decode.
const images = new Map();
const queue = [];
let active = 0;
function pump() {
  while (active < 4 && queue.length) {
    const { image, url, resolve } = queue.shift();
    active++;
    image.src = url;
    image.decode().catch(() => {}).finally(() => {
      active--;
      resolve();
      pump();
    });
  }
}
function warm(url) {
  if (!url?.startsWith('assets/')) return Promise.resolve();
  const existing = images.get(url);
  if (existing) { images.delete(url); images.set(url, existing); return existing.ready; }
  const image = new Image();
  const ready = new Promise((resolve) => queue.push({ image, url, resolve }));
  images.set(url, { image, ready });
  if (images.size > 128) images.delete(images.keys().next().value);
  pump();
  return ready;
}
export function warmBanners(apps) {
  return Promise.all(apps.filter(Boolean).flatMap((app) => {
    const b = app.banner || {};
    return [b.bg, b.mid, b.logo, b.heroIcon, typeof app.icon === 'string' ? app.icon : null].map(warm);
  }));
}
