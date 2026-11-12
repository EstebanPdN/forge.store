// UI icons (24x24 stroke) and generated app-icon glyphs (48x48 fill).
import { fromHTML } from './dom.js';

const ui = {
  search: '<circle cx="11" cy="11" r="6.5"/><path d="m16 16 4.5 4.5"/>',
  download: '<path d="M12 4v11m-5-5 5 5 5-5"/><path d="M5 19.5h14"/>',
  sd: '<path d="M7 3h7l4 4v13a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1Z"/><path d="M9 3v4m3-4v4m3-3v3"/>',
  settings:
    '<path d="M10 2h4l.6 3 1.6 1 2.9-1 2 3.5-2.3 2v2l2.3 2-2 3.5-2.9-1-1.6 1-.6 3h-4l-.6-3-1.6-1-2.9 1-2-3.5 2.3-2v-2l-2.3-2 2-3.5 2.9 1 1.6-1Z"/><circle cx="12" cy="11.5" r="3"/>',
  back: '<path d="m14.5 6-6 6 6 6"/>',
  next: '<path d="m9.5 6 6 6-6 6"/>',
  rewind5: '<path d="M3.8 14.2A8.5 8.5 0 1 0 6 6"/><path d="m3.5 7.5 2-4.3 2.6 3.4Z" fill="currentColor" stroke="none"/><path d="M14 8.5h-4l-.4 3.4c.7-.5 1.6-.7 2.5-.7 1.5 0 2.4.9 2.4 2.3s-1 2.5-2.5 2.5c-1 0-1.8-.4-2.3-1" stroke-width="1.7"/>',
  forward5: '<path d="M20.2 14.2A8.5 8.5 0 1 1 18 6"/><path d="m20.5 7.5-2-4.3-2.6 3.4Z" fill="currentColor" stroke="none"/><path d="M14 8.5h-4l-.4 3.4c.7-.5 1.6-.7 2.5-.7 1.5 0 2.4.9 2.4 2.3s-1 2.5-2.5 2.5c-1 0-1.8-.4-2.3-1" stroke-width="1.7"/>',
  check: '<path d="m5 12.5 4.5 4.5L19 7.5"/>',
  warn: '<path d="M12 4 2.8 19.5h18.4Z"/><path d="M12 10v4.5m0 2.6v.1"/>',
  info: '<circle cx="12" cy="12" r="8.5"/><path d="M12 11v5.5m0-9v.1"/>',
  star: '<path d="m12 3.6 2.6 5.3 5.8.8-4.2 4.1 1 5.8-5.2-2.7-5.2 2.7 1-5.8-4.2-4.1 5.8-.8Z"/>',
  github:
    '<path d="M9 19c-4.3 1.4-4.3-2.2-6-2.6m12 5.2v-3.3a2.9 2.9 0 0 0-.8-2.2c2.7-.3 5.5-1.3 5.5-6a4.7 4.7 0 0 0-1.3-3.2 4.3 4.3 0 0 0-.1-3.2s-1-.3-3.4 1.3a11.6 11.6 0 0 0-6 0C6.5 2.6 5.5 2.9 5.5 2.9a4.3 4.3 0 0 0-.1 3.2A4.7 4.7 0 0 0 4 9.3c0 4.6 2.8 5.6 5.5 6a2.9 2.9 0 0 0-.8 2.2v3.3"/>',
  folder: '<path d="M3.5 7.5a2 2 0 0 1 2-2h4l2 2.2h7a2 2 0 0 1 2 2v7.8a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2Z"/>',
  folderPlus:
    '<path d="M3.5 7.5a2 2 0 0 1 2-2h4l2 2.2h7a2 2 0 0 1 2 2v7.8a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2Z"/><path d="M12 11v6m-3-3h6"/>',
  file: '<path d="M7 3h7l4 4v13a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1Z"/><path d="M14 3v4h4"/>',
  cart: '<rect x="4" y="4" width="16" height="16" rx="3"/><path d="M8 4v5h8V4M8 14h8"/>',
  box: '<path d="m12 3 8 4.5v9L12 21l-8-4.5v-9Z"/><path d="m4 7.5 8 4.5 8-4.5M12 12v9"/>',
  trash: '<path d="M5 7h14m-9 0V4.5h4V7m-7 0 .8 12.5h8.4L17 7"/>',
  play: '<path d="M8 5.5v13l10-6.5Z"/>',
  pause: '<path d="M8.5 5.5v13M15.5 5.5v13"/>',
  mute: '<path d="M4 9.5h3.5L12 5.5v13l-4.5-4H4Z"/><path d="m16 9.5 5 5m0-5-5 5"/>',
  close: '<path d="m6.5 6.5 11 11m0-11-11 11"/>',
  refresh: '<path d="M19.5 12a7.5 7.5 0 1 1-2.2-5.3M19.5 4.5v4h-4"/>',
  wifi: '<path d="M4 9.5a11.5 11.5 0 0 1 16 0M7 13a7 7 0 0 1 10 0M10 16.5a2.6 2.6 0 0 1 4 0"/><path d="M12 19.6v.1"/>',
  // Native status bar: the three arcs alone (no dot), centred in the box.
  sbWifi: '<path d="M4 11a11.5 11.5 0 0 1 16 0M7 14.5a7 7 0 0 1 10 0M10 18a2.6 2.6 0 0 1 4 0"/>',
  cpu: '<rect x="6.5" y="6.5" width="11" height="11" rx="2"/><path d="M9.5 3v3.5m5-3.5v3.5m-5 11V21m5-3.5V21M3 9.5h3.5M3 14.5h3.5m11-5H21m-3.5 5H21"/>',
  depth: '<path d="M3 8.5 12 4l9 4.5-9 4.5Z"/><path d="m3 13 9 4.5 9-4.5"/><path d="m3 17 9 4.5 9-4.5" opacity=".5"/>',
  globe: '<circle cx="12" cy="12" r="8.5"/><path d="M3.5 12h17M12 3.5c2.3 2.4 3.5 5.2 3.5 8.5s-1.2 6.1-3.5 8.5c-2.3-2.4-3.5-5.2-3.5-8.5S9.7 5.9 12 3.5Z"/>',
  link: '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1.2 1.2M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1.2-1.2"/>',
  clock: '<circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/>',
  sparkle: '<path d="M12 4.5c.6 4.6 2.4 6.4 7 7.5-4.6.6-6.4 2.4-7 7.5-.6-4.6-2.4-6.4-7-7.5 4.6-.6 6.4-2.4 7-7.5Z"/>',
  anvil: '<path d="M4 7h13.5a3 3 0 0 0 3-3H22v2.5A5.5 5.5 0 0 1 16.5 12H15l1.5 4h-9L9 12H7.5A3.5 3.5 0 0 1 4 8.5Z"/><path d="M6 20h12"/>',
  people: '<circle cx="9" cy="8.5" r="3"/><path d="M3.5 19c.6-3.2 2.7-5 5.5-5s4.9 1.8 5.5 5"/><circle cx="17" cy="9.5" r="2.4"/><path d="M15.5 14.4c2.5-.4 4.5 1.2 5 4.1"/>',
  chip: '<rect x="4" y="6" width="16" height="12" rx="2.5"/><path d="M8 10h3v4H8zM14 10.5h2.5m-2.5 3h2.5"/>',
  wrench: '<path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.1-3.1a5.5 5.5 0 0 1-7.3 7.3l-6.2 6.2a2 2 0 0 1-2.8-2.8l6.2-6.2a5.5 5.5 0 0 1 7.3-7.3Z"/>',
  gamepad: '<path d="M7.5 7h9a4.5 4.5 0 0 1 4.4 3.6l.9 4.6a2.6 2.6 0 0 1-4.5 2.2L15.5 15.5h-7l-1.8 1.9a2.6 2.6 0 0 1-4.5-2.2l.9-4.6A4.5 4.5 0 0 1 7.5 7Z"/><path d="M8 10v3m-1.5-1.5h3"/><path d="M15.5 11h.01M17.5 12.5h.01"/>',
  grid: '<rect x="4" y="4" width="6.5" height="6.5" rx="1.5"/><rect x="13.5" y="4" width="6.5" height="6.5" rx="1.5"/><rect x="4" y="13.5" width="6.5" height="6.5" rx="1.5"/><rect x="13.5" y="13.5" width="6.5" height="6.5" rx="1.5"/>',
  update: '<path d="M12 20V8m-5 5 5-5 5 5"/><path d="M5 4.5h14"/>',
  lock: '<rect x="5.5" y="10.5" width="13" height="9.5" rx="2"/><path d="M8.5 10.5V8a3.5 3.5 0 0 1 7 0v2.5"/>',
  hammer: '<path d="m13 7 4.5 4.5M4 20l8.5-8.5"/><path d="M10.5 4.5 15 2.8l6.2 6.2-1.7 4.5-3-1.4-3.6-3.6Z"/>',
  music: '<path d="M9 18V6l11-2v12"/><circle cx="6.5" cy="18" r="2.5"/><circle cx="17.5" cy="16" r="2.5"/>',
  volume: '<path d="M5 9.5h3.5L13 5.5v13l-4.5-4H5Z"/><path d="M16.5 9a4 4 0 0 1 0 6M19 6.5a7.5 7.5 0 0 1 0 11"/>',
  palette: '<path d="M12 3.5a8.5 8.5 0 1 0 0 17c1.4 0 1.9-1 1.4-2.2-.6-1.4.3-2.8 1.8-2.8H17a3.5 3.5 0 0 0 3.5-3.5c0-4.7-3.8-8.5-8.5-8.5Z"/><circle cx="7.5" cy="11" r="1.1"/><circle cx="10.5" cy="7.5" r="1.1"/><circle cx="15" cy="8" r="1.1"/>',
  language: '<path d="M4 6h9M8.5 4v2c0 4-2.2 7-5 8.5m2.5-5c1 2 2.8 3.6 5 4.5"/><path d="m12.5 20 4-9 4 9m-6.8-2.8h5.6"/>',
  home: '<path d="M4 11 12 4l8 7v8.5a.5.5 0 0 1-.5.5H15v-6H9v6H4.5a.5.5 0 0 1-.5-.5Z"/>',
  eye: '<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12Z"/><circle cx="12" cy="12" r="3"/>',
  power: '<path d="M12 3.5v8M7 6.5a7.5 7.5 0 1 0 10 0"/>',
  layers: '<path d="M12 4 3 8.5l9 4.5 9-4.5Z"/><path d="m3 12.5 9 4.5 9-4.5"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2m0 15v2m-9.5-9.5h2m15 0h2M5.3 5.3l1.4 1.4m10.6 10.6 1.4 1.4m0-13.4-1.4 1.4M6.7 17.3l-1.4 1.4"/>',
  moon: '<path d="M19.5 14.5A8 8 0 0 1 9.5 4.5a8 8 0 1 0 10 10Z"/>',
  speed: '<path d="M4.5 16a8 8 0 1 1 15 0"/><path d="m12 13 3.5-4.5"/>',
  source: '<path d="M4 6.5C4 5 7.6 4 12 4s8 1 8 2.5S16.4 9 12 9 4 8 4 6.5Z"/><path d="M4 6.5v11C4 19 7.6 20 12 20s8-1 8-2.5v-11M4 12c0 1.5 3.6 2.5 8 2.5s8-1 8-2.5"/>',
  shield: '<path d="M12 3.5 5 6v5.5c0 4.3 3 7.6 7 9 4-1.4 7-4.7 7-9V6Z"/><path d="m9 12 2.2 2.2L15.5 10"/>',
  rom: '<rect x="5" y="3.5" width="14" height="17" rx="2"/><path d="M8.5 3.5v4h7v-4M8.5 13h7m-7 3.5h4"/>',
  cia: '<rect x="3.5" y="5" width="17" height="14" rx="3"/><path d="M8 12h8m-4-4v8"/>',
};

export function icon(name, cls = '') {
  const body = ui[name] || ui.info;
  return fromHTML(
    `<svg class="i ${cls}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`,
  );
}

// Glyphs for generated app icons (48x48, filled with the foreground colour).
const glyphs = {
  star: '<path d="m24 7.5 5 10.4 11.4 1.6-8.3 8 2 11.4L24 33.4l-10.1 5.5 2-11.4-8.3-8 11.4-1.6Z"/><circle cx="20.5" cy="24" r="1.6" fill="#241650"/><circle cx="27.5" cy="24" r="1.6" fill="#241650"/>',
  chevron: '<path d="M8 32 24 12l16 20h-9L24 22.5 17 32Z"/><path d="M4 38h12M32 38h12" stroke="currentColor" stroke-width="3" stroke-linecap="round" opacity=".55"/>',
  ship: '<path d="M24 6 34 30l-10-5-10 5Z"/><path d="M14 30 8 38l9-3M34 30l6 8-9-3" opacity=".75"/><circle cx="24" cy="18" r="2.4" fill="#1d1250"/>',
  berry: '<path d="M24 16c-8 0-12 5-12 11 0 7 6 14 12 14s12-7 12-14c0-6-4-11-12-11Z"/><path d="M24 17c-2-4-6-6-9-6 2 3 5 5 9 6Zm0 0c2-4 6-6 9-6-2 3-5 5-9 6Z" fill="#4cc38a"/><g fill="#141538" opacity=".55"><circle cx="19" cy="25" r="1.1"/><circle cx="27" cy="24" r="1.1"/><circle cx="23" cy="30" r="1.1"/><circle cx="29" cy="31" r="1.1"/><circle cx="18" cy="33" r="1.1"/><circle cx="24" cy="36" r="1.1"/></g>',
  hex: '<path d="M24 6 39.6 15v18L24 42 8.4 33V15Z" fill="none" stroke="currentColor" stroke-width="3"/><path d="M24 15 31.8 19.5v9L24 33l-7.8-4.5v-9Z"/><path d="M24 2.5v6M24 39.5v6M5.5 13.5l5 3M37.5 31.5l5 3" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" opacity=".5"/>',
  rune: '<path d="M24 6c9.9 0 16 7 16 16 0 7.6-4.6 13.3-11 15.2V42h-3V18h-4v24h-3v-4.8C12.6 35.3 8 29.6 8 22 8 13 14.1 6 24 6Zm-5 4.6C14 12.6 11 17 11 22c0 5.5 3.3 9.9 8 11.7Zm10 0v23.1c4.7-1.8 8-6.2 8-11.7 0-5-3-9.4-8-11.4Z"/>',
  scion: '<path d="M24 6 41 38H7Z" opacity=".35"/><path d="M24 14 34.5 34h-21Z"/><path d="M24 21v8" stroke="#15110a" stroke-width="2.6" stroke-linecap="round"/>',
  visor: '<rect x="6" y="15" width="36" height="17" rx="5"/><rect x="10" y="19" width="11" height="9" rx="2" fill="#000" opacity=".75"/><rect x="27" y="19" width="11" height="9" rx="2" fill="#000" opacity=".75"/><path d="M20 32h8v8h-8z" opacity=".7"/>',
  gba: '<path d="M9 14h30a5 5 0 0 1 5 5v10a5 5 0 0 1-5 5H9a5 5 0 0 1-5-5V19a5 5 0 0 1 5-5Z"/><rect x="15" y="17" width="18" height="14" rx="1.5" fill="#3a1d78"/><circle cx="9.5" cy="24" r="2.5" fill="#3a1d78"/><circle cx="38.5" cy="22" r="1.7" fill="#3a1d78"/><circle cx="35.5" cy="26" r="1.7" fill="#3a1d78"/>',
  save: '<path d="M10 8h23l7 7v23a2 2 0 0 1-2 2H10a2 2 0 0 1-2-2V10a2 2 0 0 1 2-2Z"/><rect x="15" y="8" width="15" height="10" rx="1" fill="#13615a"/><rect x="14" y="26" width="20" height="14" rx="1.5" fill="#13615a" opacity=".55"/><path d="M26 10h3v6h-3z"/>',
  flower: '<g transform="translate(24 24)"><ellipse rx="5.5" ry="12" transform="rotate(0) translate(0 -9)"/><ellipse rx="5.5" ry="12" transform="rotate(72) translate(0 -9)"/><ellipse rx="5.5" ry="12" transform="rotate(144) translate(0 -9)"/><ellipse rx="5.5" ry="12" transform="rotate(216) translate(0 -9)"/><ellipse rx="5.5" ry="12" transform="rotate(288) translate(0 -9)"/><circle r="5.5" fill="#ffd34d"/></g>',
  box: '<path d="m24 6 16 9v18l-16 9-16-9V15Z"/><path d="m8 15 16 9 16-9M24 24v18" stroke="#163f8a" stroke-width="2" fill="none" opacity=".7"/><path d="M24 30v-9m-4 5 4 4 4-4" stroke="#163f8a" stroke-width="2.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  globe: '<circle cx="24" cy="24" r="16"/><path d="M8 24h32M24 8c5 4.6 7 10 7 16s-2 11.4-7 16c-5-4.6-7-10-7-16s2-11.4 7-16Z" fill="none" stroke="#0d5a35" stroke-width="2.2"/>',
};

/** Generated icon: gradient tile + glyph. Returns an <svg> element. */
export function glyphIcon(spec, size = 48) {
  const [c1, c2] = spec.bg || ['#55606b', '#2b3036'];
  const id = 'g' + Math.random().toString(36).slice(2, 8);
  return fromHTML(`<svg viewBox="0 0 48 48" width="${size}" height="${size}" aria-hidden="true">
    <defs>
      <linearGradient id="${id}" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="${c1}"/><stop offset="1" stop-color="${c2}"/></linearGradient>
      <radialGradient id="${id}s" cx=".3" cy=".2" r=".9"><stop offset="0" stop-color="#fff" stop-opacity=".28"/><stop offset=".6" stop-color="#fff" stop-opacity="0"/></radialGradient>
    </defs>
    <rect width="48" height="48" fill="url(#${id})"/>
    <rect width="48" height="48" fill="url(#${id}s)"/>
    <g fill="${spec.fg || '#fff'}" color="${spec.fg || '#fff'}">${glyphs[spec.glyph] || glyphs.star}</g>
  </svg>`);
}
