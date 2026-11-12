#!/usr/bin/env python3
"""Builds romfs/ for the native 3DS store from the rendered assets.

Textures are written as .ftx sheets: several images packed into one GPU texture,
already swizzled (8x8 Morton tiles) and zlib-compressed, so the console only
inflates and copies them. tex3ds is not used because the devkitPro build on this
Mac is a Linux binary.

.ftx layout (little endian):
  'FTX1' u16 texW u16 texH u8 format(0 RGBA8, 1 RGB565, 2 RGBA4, 3 A8, 4 RGB8) u8 count u16 pad
  count * (u16 x, u16 y, u16 w, u16 h)
  u32 rawSize u32 zSize, zlib(data)

Usage: pack.py <project-root> [--fonts-only | --ui-only]
--fonts-only updates only fonts.bin/glyphs-*.ftx; never clears romfs or packages the app.
--ui-only updates only gfx/ui.ftx and ui.json (brand pieces, badges, status-bar emblem).
"""
import base64
import json
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
GEN = ROOT / '3ds/build/gen'
AUD = ROOT / '3ds/build/audio'
OUT = ROOT / '3ds/romfs'
RGBA8, RGB565, RGBA4, A8, RGB8 = 0, 1, 2, 3, 4


def pow2(n):
    p = 8
    while p < n:
        p *= 2
    return p


def morton_index(w, h):
    y, x = np.mgrid[0:h, 0:w]
    m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)
    return ((((y >> 3) * (w >> 3) + (x >> 3)) << 6) + m).ravel()


def encode(canvas, fmt):
    h, w = canvas.shape[:2]
    idx = morton_index(w, h)
    if fmt == A8:
        out = np.zeros(w * h, np.uint8)
        out[idx] = canvas.reshape(-1)
        return out.tobytes()
    px = canvas.reshape(-1, 4).astype(np.uint32)
    r, g, b, a = px[:, 0], px[:, 1], px[:, 2], px[:, 3]
    if fmt == RGBA8:
        out = np.zeros((w * h, 4), np.uint8)
        out[idx] = px[:, [3, 2, 1, 0]].astype(np.uint8)
        return out.tobytes()
    if fmt == RGB8:
        out = np.zeros((w * h, 3), np.uint8)
        out[idx] = px[:, [2, 1, 0]].astype(np.uint8)
        return out.tobytes()
    if fmt == RGB565:
        # 4x4 ordered dither: soft gradients in the banners don't band at 5-6-5 bits.
        bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) / 16 - 0.5 + 1 / 32
        yy, xx = np.mgrid[0:h, 0:w]
        d = bayer[yy & 3, xx & 3].ravel()
        q = lambda c, step: np.clip(np.floor(c.astype(np.float32) / step + d + 0.5), 0, 255 // step).astype(np.uint32)
        v = (q(r, 8) << 11) | (q(g, 4) << 5) | q(b, 8)
    else:
        v = ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4)
    out = np.zeros(w * h, np.uint16)
    out[idx] = v.astype(np.uint16)
    return out.astype('<u2').tobytes()


def shelf_pack(sizes, max_w=1024):
    """Simple shelf packer with 1 px gutters. Returns positions and texture size."""
    order = sorted(range(len(sizes)), key=lambda i: -sizes[i][1])
    pos = [None] * len(sizes)
    x = y = shelf = 0
    width = min(max_w, pow2(max(w for w, _ in sizes) + 2))
    while True:
        x = y = shelf = 0
        ok = True
        for i in order:
            w, h = sizes[i][0] + 2, sizes[i][1] + 2
            if x + w > width:
                x, y, shelf = 0, y + shelf, 0
            pos[i] = (x + 1, y + 1)
            x += w
            shelf = max(shelf, h)
        height = y + shelf
        if height <= width or width >= max_w:
            break
        width *= 2
    return pos, width, pow2(height)


def write_sheet(path, images, fmt, opaque_bg=(0, 0, 0, 0)):
    """images: list of PIL RGBA images. Writes one .ftx with all of them."""
    sizes = [im.size for im in images]
    pos, tw, th = shelf_pack(sizes)
    if tw > 1024 or th > 1024:
        raise SystemExit(f'{path}: atlas too big ({tw}x{th})')
    canvas = np.zeros((th, tw, 4), np.uint8)
    canvas[:, :] = opaque_bg
    for im, (x, y) in zip(images, pos):
        arr = np.asarray(im.convert('RGBA'))
        h, w = arr.shape[:2]
        canvas[y:y + h, x:x + w] = arr
        # Repeat the edge pixels into the gutter so linear filtering never bleeds.
        canvas[y - 1, x:x + w] = arr[0]
        canvas[y + h, x:x + w] = arr[-1]
        canvas[y - 1:y + h + 1, x - 1] = canvas[y - 1:y + h + 1, x]
        canvas[y - 1:y + h + 1, x + w] = canvas[y - 1:y + h + 1, x + w - 1]
    raw = encode(canvas, fmt)
    z = zlib.compress(raw, 9)
    head = struct.pack('<4sHHBBH', b'FTX1', tw, th, fmt, len(images), 0)
    rects = b''.join(struct.pack('<HHHH', x, y, im.size[0], im.size[1]) for im, (x, y) in zip(images, pos))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(head + rects + struct.pack('<II', len(raw), len(z)) + z)
    return tw, th, len(z)


def write_alpha_page(path, canvas):
    h, w = canvas.shape
    raw = encode(canvas, A8)
    z = zlib.compress(raw, 9)
    head = struct.pack('<4sHHBBH', b'FTX1', w, h, A8, 0, 0)
    path.write_bytes(head + struct.pack('<II', len(raw), len(z)) + z)
    return len(z)


def pack_glyphs():
    """Fonts and UI icons baked by render-fonts.mjs -> A8 pages + fonts.bin.

    fonts.bin (little endian):
      'FNT3' u16 nstyles u16 npages u32 nglyphs
      nstyles * (u8 kind(0 font, 1 icon) u8 family u16 weight f32 size f32 ascent f32 descent f32 line u32 first u32 count)
      nglyphs * (u32 cp u16 x u16 y u8 w u8 h s8 ox s8 oy u16 adv*64 u8 page u8 inkInsets), sorted by cp per style
      inkInsets: low/high nibble = left/right fringe outside coverage >= 32/255.
      u16 nicons, nicons * (u8 len, name)
    """
    d = json.loads((GEN / 'glyphs.json').read_text())
    # Keep native FreeType grayscale coverage. Legacy browser inputs may opt into contrast.
    contrast = d.get('coverageContrast', 1)
    coverage = np.arange(256, dtype=np.float32) / 255
    coverage = np.rint(np.clip((coverage - 0.5) * contrast + 0.5, 0, 1) * 255).astype(np.uint8)
    if len(d['styles']) > 96 or len(d['icons']) > 64:
        raise ValueError('Font styles/icons exceed gfx.c limits')
    stage = GEN / 'font-pack'
    stage.mkdir(parents=True, exist_ok=True)
    (OUT / 'gfx').mkdir(parents=True, exist_ok=True)
    entries = []
    for si, st in enumerate(d['styles']):
        for g in sorted(st['glyphs'], key=lambda g: g['cp']):
            entries.append((si, g))
    PAGE = 1024
    order = sorted((i for i, (_, g) in enumerate(entries) if g['w']), key=lambda i: (-entries[i][1]['h'], -entries[i][1]['w']))
    pages, place = [], {}
    canvas = np.zeros((PAGE, PAGE), np.uint8)
    x = y = shelf = 0
    shared = {}
    for i in order:
        g = entries[i][1]
        w, h = g['w'], g['h']
        if not (0 < w < 255 and 0 < h < 255):
            raise ValueError('Glyph dimensions exceed FNT3 fields')
        key = (w, h, g['a'])
        if key in shared:
            place[i] = shared[key]
            continue
        if x + w + 1 > PAGE:
            x, y, shelf = 0, y + shelf, 0
        if y + h + 1 > PAGE:
            pages.append((canvas, PAGE))
            canvas = np.zeros((PAGE, PAGE), np.uint8)
            x = y = shelf = 0
        alpha = np.frombuffer(base64.b64decode(g['a']), np.uint8).reshape(h, w)
        canvas[y + 1:y + 1 + h, x + 1:x + 1 + w] = coverage[alpha]
        place[i] = (len(pages), x + 1, y + 1)
        shared[key] = place[i]
        x += w + 1
        shelf = max(shelf, h + 1)
    pages.append((canvas, min(PAGE, pow2(y + shelf + 1))))
    if len(pages) > 16:
        raise ValueError("Font pages exceed gfx.c limit")
    sizes = []
    for n, (c, used_h) in enumerate(pages):
        sizes.append(write_alpha_page(stage / f'glyphs-{n}.ftx', c[:used_h]))
    out = bytearray(struct.pack('<4sHHI', b'FNT3', len(d['styles']), len(pages), len(entries)))
    first = 0
    for st in d['styles']:
        n = len(st['glyphs'])
        out += struct.pack('<BBHffffII', 1 if st['kind'] == 'icon' else 0, st['family'], st['weight'], st['size'], st['ascent'], st['descent'], st['line'], first, n)
        first += n
    for i, (_, g) in enumerate(entries):
        pg, gx, gy = place.get(i, (0, 0, 0))
        inset = 0
        if g['w']:
            alpha = coverage[np.frombuffer(base64.b64decode(g['a']), np.uint8).reshape(g['h'], g['w'])]
            columns = np.flatnonzero(np.any(alpha >= 32, axis=0))
            if len(columns):
                left, right = int(columns[0]), g['w'] - 1 - int(columns[-1])
                # A conservative fallback keeps unusual faint/oversized glyphs safe.
                if left <= 15 and right <= 15: inset = left | (right << 4)
        out += struct.pack('<IHHBBbbHBB', g['cp'], gx, gy, g['w'], g['h'], g['ox'], g['oy'], round(g['adv'] * 64), pg, inset)
    out += struct.pack('<H', len(d['icons']))
    for name in d['icons']:
        b = name.encode()
        out += struct.pack('<B', len(b)) + b
    # Sidecar keeps FNT3 compatible while supplying exact 1/64-pixel pair deltas.
    kern=bytearray(struct.pack('<4sH',b'KRN1',len(d['styles'])))
    for st in d['styles']:
        pairs=st.get('kerning',[])
        kern+=struct.pack('<I',len(pairs))
        for a,b,delta in pairs:kern+=struct.pack('<IIh',a,b,delta)
    (OUT/'gfx/kerning.bin').write_bytes(kern)
    (stage / 'fonts.bin').write_bytes(out)
    for n in range(len(pages)):
        shutil.copyfile(stage / f'glyphs-{n}.ftx', OUT / f'gfx/glyphs-{n}.ftx')
    # Write the index last; consumers see a complete matching atlas after the command exits.
    shutil.copyfile(stage / 'fonts.bin', OUT / 'gfx/fonts.bin')
    for path in (OUT / 'gfx').glob('glyphs-*.ftx'):
        if int(path.stem.split('-')[1]) >= len(pages): path.unlink()
    report = {'format':'FNT3', 'styles':len(d['styles']), 'glyphs':len(entries),
              'uniqueBitmaps':len(shared), 'pages':[[PAGE,h] for _,h in pages],
              'textureBytes':sum(PAGE*h for _,h in pages), 'indexBytes':len(out),
              'compressedPageBytes':sum(sizes)+20*len(pages)}
    (GEN / 'font-pack-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'glyphs: {len(entries)} in {len(pages)} pages ({sum(sizes) // 1024} KB)')


def coin_frame(name):
    """A 4x render from render-coins.mjs reduced to 1x with an area filter (premultiplied)."""
    im = Image.open(GEN / f'coins/{name}.png').convert('RGBa')
    return im.resize((im.width // 4, im.height // 4), Image.BOX).convert('RGBA')


def load(path, size=None):
    im = Image.open(path).convert('RGBA')
    if size:
        im = im.resize(size, Image.LANCZOS)
    return im


def pack_ui():
    """Interface atlas: brand pieces and button badges. Names -> index file."""
    ui_names = ['emblem', 'emblem-small', 'title', 'welcome', 'wordmark']
    ui_files = [GEN / f'{n}.png' for n in ui_names]
    ui_extra = {'halo': coin_frame('halo'), 'coin-shadow': coin_frame('shadow'), 'sb-emblem': coin_frame('sb-emblem')}
    for p in sorted((GEN / 'ui').glob('badge-*.png')):  # UI icons come from the glyph atlas
        ui_names.append(p.stem)
        ui_files.append(p)
    ui_images = [load(p) for p in ui_files] + list(ui_extra.values())
    ui_names += list(ui_extra)
    result = write_sheet(OUT / 'gfx/ui.ftx', ui_images, RGBA8)
    (OUT / 'gfx/ui.json').write_text(json.dumps(ui_names))
    return result


def main():
    if OUT.exists():
        shutil.rmtree(OUT)
    (OUT / 'gfx').mkdir(parents=True)
    catalog = json.loads((ROOT / 'catalog/forge-store.json').read_text())
    layers = json.loads((GEN / 'layers.json').read_text())
    apps = [a['id'] for a in catalog['apps']]
    stats = []

    stats.append(('ui', *pack_ui()))

    # 3D coins (render-coins.mjs renders them at 4x): one sheet of wobble poses per coin, one of
    # spin poses for the theme-picker coins.
    coins = json.loads((GEN / 'coins/coins.json').read_text())
    for name, m in coins.items():
        stats.append((f'coin-{name}', *write_sheet(OUT / f'gfx/coin-{name}.ftx', [coin_frame(f'{name}-w{i:02d}') for i in range(m['wobble'])], RGBA8)))
        if m['spin']:
            stats.append((f'coin-{name}-spin', *write_sheet(OUT / f'gfx/coin-{name}-spin.ftx', [coin_frame(f'{name}-s{i:02d}') for i in range(m['spin'])], RGBA8)))
    (OUT / 'gfx/coins.json').write_text(json.dumps(coins))
    tiles = json.loads((GEN / 'coins/tiles.json').read_text())
    stats.append(('tiles', *write_sheet(OUT / 'gfx/tiles.ftx', [coin_frame(f'tile-{n}') for n in tiles['names']], RGBA8)))
    (OUT / 'gfx/tiles.json').write_text(json.dumps(tiles))

    # App icons in catalog order, at every size the store draws them (1:1, never scaled).
    stats.append(('icons', *write_sheet(OUT / 'gfx/icons.ftx', [load(GEN / f'icons/{a}.png') for a in apps], RGBA8)))
    for size in [20, 32, 40, 44, 64]:
        stats.append((f'icons-{size}', *write_sheet(OUT / f'gfx/icons-{size}.ftx', [load(GEN / f'icons-{size}/{a}.png') for a in apps], RGBA8)))
    pack_glyphs()

    # Screen backgrounds (7 px margin for the dot drift). 24-bit: the soft haze bands in RGB565.
    for name in ['bg-top-light', 'bg-top-dark', 'bg-bottom-light', 'bg-bottom-dark']:
        stats.append((name, *write_sheet(OUT / f'gfx/{name}.ftx', [load(GEN / f'{name}.png')], RGB8)))

    # Banners: opaque background (RGB565) and trimmed mid/logo layers (RGBA8).
    banner_meta = {}
    for a in apps:
        L = layers.get(a, {})
        if (GEN / f'banners/{a}-bg.png').exists():
            write_sheet(OUT / f'gfx/banner/{a}-bg.ftx', [load(GEN / f'banners/{a}-bg.png')], RGB565)
        fg, meta = [], {}
        for layer in ['mid', 'logo']:
            box = L.get(layer)
            if not box:
                continue
            im = load(GEN / f'banners/{a}-{layer}.png').crop((box['x'], box['y'], box['x'] + box['w'], box['y'] + box['h']))
            meta[layer] = {'i': len(fg), **box}
            fg.append(im)
        if fg:
            write_sheet(OUT / f'gfx/banner/{a}-fg.ftx', fg, RGBA8)
        banner_meta[a] = meta
    (OUT / 'gfx/banners.json').write_text(json.dumps(banner_meta))

    # Screenshots: top 400x240 and bottom 320x240 per app (loaded when a page opens).
    shots_meta = {}
    for app in catalog['apps']:
        tops, bottoms = [], []
        for s in app.get('shots', []):
            tops.append(load(ROOT / s['top'], (400, 240)))
            bottoms.append(load(ROOT / s['bottom'], (320, 240)) if s.get('bottom') else None)
        if not tops:
            continue
        # Up to 4 per sheet keeps every atlas within 1024x1024.
        sheets = []
        for i in range(0, len(tops), 4):
            write_sheet(OUT / f"gfx/shots/{app['id']}-t{i // 4}.ftx", tops[i:i + 4], RGB565)
            sheets.append(len(tops[i:i + 4]))
        real_bottoms = [b for b in bottoms if b is not None]
        if real_bottoms:
            for i in range(0, len(real_bottoms), 4):
                write_sheet(OUT / f"gfx/shots/{app['id']}-b{i // 4}.ftx", real_bottoms[i:i + 4], RGB565)
        shots_meta[app['id']] = {'count': len(tops), 'bottom': [b is not None for b in bottoms]}
    (OUT / 'gfx/shots.json').write_text(json.dumps(shots_meta))

    # Audio: synthesized effects and music, and the site's intro sounds.
    (OUT / 'audio').mkdir()
    for p in AUD.glob('*.pcm'):
        shutil.copy(p, OUT / 'audio' / p.name)
    for name, src in [('intro-welcome', 'assets/audio/intro-welcome.mp3'), ('intro-enter', 'assets/audio/intro-enter.mp3')]:
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', str(ROOT / src), '-ac', '2', '-ar', '32000', '-f', 's16le',
                        str(OUT / f'audio/{name}.pcm')], check=True)

    # Data
    shutil.copy(ROOT / 'catalog/forge-store.json', OUT / 'catalog.json')
    shutil.copy(ROOT / '3ds/build/strings.json', OUT / 'strings.json')
    shutil.copy(ROOT / '3ds/meta/cacert.pem', OUT / 'cacert.pem')

    total = sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file())
    for name, tw, th, z in stats:
        print(f'{name:18s} {tw}x{th}  {z // 1024} KB')
    print(f'romfs: {total / 1048576:.1f} MB')


if __name__ == '__main__':
    if '--fonts-only' in sys.argv[2:]:
        pack_glyphs()
    elif '--ui-only' in sys.argv[2:]:  # only gfx/ui.ftx + ui.json, like --fonts-only for glyphs
        print('ui', *pack_ui())
    else:
        main()
