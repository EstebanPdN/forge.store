#!/usr/bin/env python3
"""Render only the Forge home-grid icons, entirely offline (no browser).

Requires Pillow, NumPy, Node and an installed Sharp module. From the repo root:
  3ds/tools/.venv/bin/python -I 3ds/tools/render-home-icons.py --write-ftx
If Sharp is outside Node's normal resolution path, add --sharp-module /path/to/sharp.
Without --write-ftx only 3ds/build/gen/icons-44 is written. --check verifies existing
outputs without writing. No pack.py import/execution, compilation or packaging.

Artwork and glyph geometry come from the catalog and the web's glyphIcon function.
The fixed composition mirrors ui.css and native-ui.css, not the old 48 px renders.
See report.md for renderer differences and the native integration still required.
"""
import argparse
import base64
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import zlib

import numpy as np
from PIL import Image, ImageDraw, __version__ as PIL_VERSION

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / '3ds/build/gen/icons-44'
FTX = ROOT / '3ds/romfs/gfx/icons-44.ftx'
SIZE, RADIUS, AA, COUNT = 44, 10, 8, 23

# Only a template shim: no browser, layout engine, server or network. Calling the
# real glyphIcon avoids copying SVG paths or reimplementing its gradient geometry.
SVG_WORKER = r"""
const {createRequire} = require('node:module');
const {pathToFileURL} = require('node:url');
const fs = require('node:fs');
const path = require('node:path');
(async () => {
  const input = JSON.parse(fs.readFileSync(0, 'utf8'));
  const sharp = createRequire(path.join(input.root, '3ds/tools/render-home-icons.cjs'))(input.sharp);
  globalThis.document = {createElement(tag) {
    if (tag !== 'template') throw new Error('Unexpected DOM use: ' + tag);
    return {set innerHTML(value) {this.content = {firstElementChild: value};}};
  }};
  const {glyphIcon} = await import(pathToFileURL(path.join(input.root, 'src/js/core/icons.js')));
  const images = {};
  for (const app of input.apps) {
    let svg = glyphIcon(app.icon, input.size);
    const id = svg.match(/id="(g[a-z0-9]+)"/)[1];
    svg = svg.split(id).join('forge-home-glyph');
    svg = svg.replace('<svg ', '<svg xmlns="http://www.w3.org/2000/svg" ');
    images[app.id] = (await sharp(Buffer.from(svg)).png().toBuffer()).toString('base64');
  }
  process.stdout.write(JSON.stringify({images, versions: sharp.versions}));
})().catch(e => {console.error(e.message); process.exitCode = 1;});
"""


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_catalog():
    apps = json.loads((ROOT / 'catalog/forge-store.json').read_text())['apps']
    ids = [a['id'] for a in apps]
    require(len(ids) == COUNT and len(set(ids)) == COUNT, 'Expected 23 unique Forge catalog IDs')
    require(all(re.fullmatch(r'[a-z0-9_-]+', x) for x in ids), 'Unsafe icon ID')
    for a in apps:
        if isinstance(a['icon'], str):
            source = (ROOT / a['icon']).resolve()
            require(source.is_relative_to(ROOT / 'assets'), 'Only local artwork under assets/ is supported')
            require(source.is_file(), f'Missing icon: {source}')
    # Native indices come from this array, not from alphabetical/display sorting.
    native = ROOT / '3ds/romfs/catalog.json'
    if native.exists():
        order = [a['id'] for a in json.loads(native.read_text())['apps']]
        require(order == ids, 'romfs/catalog.json order differs; reconcile before generating a sheet')
    return apps


def round_shape(inset=0, dy=0):
    """8x pixel-center coverage for a CSS circular rounded rectangle."""
    y, x = np.mgrid[:SIZE * AA, :SIZE * AA].astype(np.float64)
    x, y = (x + .5) / AA, (y + .5) / AA - dy
    lo, hi, r = inset, SIZE - inset, RADIUS - inset
    dx = np.maximum(np.maximum(lo + r - x, x - (hi - r)), 0)
    dy = np.maximum(np.maximum(lo + r - y, y - (hi - r)), 0)
    return (x >= lo) & (x <= hi) & (y >= lo) & (y <= hi) & (dx * dx + dy * dy <= r * r)


def render_icon(app, glyphs):
    n = SIZE * AA
    if isinstance(app['icon'], str):
        with Image.open(ROOT / app['icon']) as source:
            art = source.convert('RGBA')
        sampling = Image.Resampling.NEAREST if app.get('pixelIcon') else Image.Resampling.LANCZOS
        # Filter artwork once at the target size; supersampling below is only for
        # the corner/border composition. Mutiny's pixel art stays nearest sampled.
        art = art.resize((SIZE, SIZE), sampling).resize((n, n), Image.Resampling.NEAREST)
    else:
        art = Image.open(io.BytesIO(base64.b64decode(glyphs[app['id']]))).convert('RGBA')
        require(art.size == (n, n), f'Unexpected SVG size for {app["id"]}')
    px = np.asarray(art).astype(np.float64) / 255
    rgb = px[:, :, :3]
    if app.get('status') == 'development':
        # CSS filter order and sRGB saturation coefficients, not Pillow's luma.
        luma = rgb @ np.array([.213, .715, .072])
        rgb = (luma[:, :, None] + .15 * (rgb - luma[:, :, None])) * .86
        rgb = np.clip((rgb - .5) * .92 + .5, 0, 1)
    # The current RVC source has alpha 254 everywhere. Match the light web tile
    # background; its dark-theme difference is recorded rather than hidden.
    alpha = px[:, :, 3:4]
    rgb = rgb * alpha + np.array([238, 242, 244]) / 255 * (1 - alpha)
    outer = round_shape()
    border = outer & ~round_shape(inset=1)
    highlight = outer & ~round_shape(dy=1)
    # ::after lists the white top inset first, so it is painted above the black rim.
    rgb[border] *= .92
    rgb[highlight] = rgb[highlight] * .65 + .35
    rgba = np.dstack((np.rint(rgb * 255).astype(np.uint8), outer.astype(np.uint8) * 255))
    rgba[~outer] = 0
    return Image.fromarray(rgba).convert('RGBa').resize((SIZE, SIZE), Image.Resampling.BOX).convert('RGBA')


def ftx_bytes(images):
    """FTX1 RGBA8, same shelf layout and ABGR Morton encoding as pack.py.

    This small standalone writer cannot invoke pack.py's destructive main().
    Rect order stays catalog order; 23 padded 44 px cells fit a 256x256 texture.
    """
    w = h = 256
    canvas = np.zeros((h, w, 4), dtype=np.uint8)
    rects = []
    for i, im in enumerate(images):
        x, y = (i % 5) * 46 + 1, (i // 5) * 46 + 1
        rects.append((x, y, SIZE, SIZE))
        # One duplicated texel on all four sides prevents sampling neighbours.
        canvas[y - 1:y + SIZE + 1, x - 1:x + SIZE + 1] = np.pad(
            np.asarray(im), ((1, 1), (1, 1), (0, 0)), mode='edge')
    yy, xx = np.mgrid[:h, :w]
    morton = ((xx & 1) | ((yy & 1) << 1) | ((xx & 2) << 1) |
              ((yy & 2) << 2) | ((xx & 4) << 2) | ((yy & 4) << 3))
    indices = (((yy // 8) * (w // 8) + xx // 8) * 64 + morton).ravel()
    raw = np.empty((w * h, 4), dtype=np.uint8)
    raw[indices] = canvas.reshape(-1, 4)[:, ::-1]
    compressed = zlib.compress(raw.tobytes(), 9)
    header = struct.pack('<4sHHBBH', b'FTX1', w, h, 0, len(images), 0)
    header += b''.join(struct.pack('<HHHH', *rect) for rect in rects)
    return header + struct.pack('<II', raw.nbytes, len(compressed)) + compressed


def verify_ftx(data, images):
    """Decode independently with scalar address math; compare every image/gutter."""
    magic, w, h, fmt, count, pad = struct.unpack_from('<4sHHBBH', data)
    require((magic, w, h, fmt, count, pad) == (b'FTX1', 256, 256, 0, COUNT, 0), 'Bad FTX header')
    offset = 12 + count * 8
    raw_size, zsize = struct.unpack_from('<II', data, offset)
    require(len(data) == offset + 8 + zsize, 'Bad FTX payload size')
    raw = zlib.decompress(data[offset + 8:])
    require(raw_size == len(raw) == w * h * 4, 'Bad FTX raw size')
    decoded = Image.new('RGBA', (w, h))
    pixels = decoded.load()
    for y in range(h):
        for x in range(w):
            tile = (y // 8) * (w // 8) + x // 8
            local = sum((((x >> b) & 1) << (2 * b)) | (((y >> b) & 1) << (2 * b + 1)) for b in range(3))
            address = (tile * 64 + local) * 4
            pixels[x, y] = tuple(raw[address:address + 4][::-1])
    for i, im in enumerate(images):
        rect = struct.unpack_from('<HHHH', data, 12 + 8 * i)
        x, y, iw, ih = rect
        require(rect == ((i % 5) * 46 + 1, (i // 5) * 46 + 1, SIZE, SIZE), f'Bad rect {i}')
        require(decoded.crop((x, y, x + iw, y + ih)).tobytes() == im.tobytes(), f'FTX image/order mismatch {i}')
        gutter = np.asarray(decoded.crop((x - 1, y - 1, x + iw + 1, y + ih + 1)))
        require(np.array_equal(gutter, np.pad(np.asarray(im), ((1, 1), (1, 1), (0, 0)), mode='edge')),
                f'FTX gutter mismatch {i}')


def verify_icons(apps, images):
    require(len(images) == COUNT, 'Wrong icon count')
    expected_alpha = np.asarray(Image.fromarray(round_shape().astype(np.uint8) * 255).resize(
        (SIZE, SIZE), Image.Resampling.BOX))
    # Independent analytic quarter-circle area: each top corner has area
    # r*r*(1-pi/4). The sampling tolerance is less than half a native pixel.
    corner_area = (255 - expected_alpha[:RADIUS, :RADIUS].astype(float)).sum() / 255
    require(abs(corner_area - RADIUS**2 * (1 - math.pi / 4)) < .5, 'Wrong corner radius')
    for app, im in zip(apps, images):
        require(im.mode == 'RGBA' and im.size == (SIZE, SIZE), f'Bad dimensions/mode: {app["id"]}')
        alpha = np.asarray(im)[:, :, 3]
        require(np.array_equal(alpha, expected_alpha), f'Bad rounded alpha: {app["id"]}')
        require(np.array_equal(alpha, alpha[::-1]) and np.array_equal(alpha, alpha[:, ::-1]), 'Asymmetric corners')
        require(alpha[0, 0] == 0 and alpha[0, RADIUS] == 255 and alpha[22, 22] == 255, 'Bad coverage')


def contact_sheet(apps, images):
    sheet = Image.new('RGB', (800, 5 * 204 + 36), '#20252b')
    draw = ImageDraw.Draw(sheet)
    draw.text((12, 10), 'Forge home icons | catalog order, 0-based | 1x + nearest 2x | 44px / r10', fill='white')
    for i, (app, im) in enumerate(zip(apps, images)):
        x, y = (i % 5) * 160, 36 + (i // 5) * 204
        draw.text((x + 8, y + 6), f'{i:02d}  {app["id"]}', fill='white')
        for top, scale, bg in [(y + 26, 1, '#eef2f4'), (y + 82, 2, '#2f3236')]:
            tile = Image.new('RGBA', (SIZE * scale, SIZE * scale), bg)
            tile.alpha_composite(im.resize(tile.size, Image.Resampling.NEAREST))
            sheet.paste(tile.convert('RGB'), (x + 8, top))
        if app.get('status') == 'development':
            draw.text((x + 8, y + 181), 'development filter', fill='#adb9c3')
    return sheet


def source_hashes(apps):
    names = ['catalog/forge-store.json', 'src/js/core/icons.js', 'src/js/core/dom.js',
             'src/js/ui/components.js', 'src/css/ui.css', 'src/css/native-ui.css',
             'src/css/tokens.css', 'docs/UI-V2-REVIEW-2026-11-10.md',
             '3ds/tools/render-home-icons.py']
    names += [a['icon'] for a in apps if isinstance(a['icon'], str)]
    return {name: sha((ROOT / name).read_bytes()) for name in names}


def check(apps, expect_ftx):
    manifest = json.loads((OUT / 'manifest.json').read_text())
    require(manifest['source_sha256'] == source_hashes(apps), 'Sources changed; regenerate icons')
    require([e['id'] for e in manifest['icons']] == [a['id'] for a in apps], 'Manifest order mismatch')
    require({p.stem for p in OUT.glob('*.png')} == {a['id'] for a in apps} | {'contact-sheet'}, 'Unexpected/missing PNG')
    images = []
    for i, entry in enumerate(manifest['icons']):
        require(entry['index'] == i and entry['file'] == f'{apps[i]["id"]}.png', 'Bad index/filename')
        path = OUT / entry['file']
        require(sha(path.read_bytes()) == entry['sha256'], f'PNG hash mismatch: {path.name}')
        with Image.open(path) as im:
            images.append(im.copy())
    verify_icons(apps, images)
    if expect_ftx:
        require(manifest['ftx'] is not None, 'No FTX was generated in this manifest')
        require(sha(FTX.read_bytes()) == manifest['ftx']['sha256'], 'FTX hash mismatch')
        verify_ftx(FTX.read_bytes(), images)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--sharp-module', default=os.environ.get('FORGE_SHARP_MODULE', 'sharp'),
                        help='Installed Sharp module name or absolute directory; never downloads dependencies')
    parser.add_argument('--write-ftx', action='store_true', help='Also write/verify ONLY romfs/gfx/icons-44.ftx')
    parser.add_argument('--check', action='store_true', help='Verify outputs and source hashes without rendering/writing')
    args = parser.parse_args()
    apps = read_catalog()
    if args.check:
        check(apps, args.write_ftx)
        print(f'PASS: {COUNT} RGBA icons, {SIZE}x{SIZE}, radius {RADIUS}, catalog order' +
              ('; FTX pixels and gutters match exactly' if args.write_ftx else ''))
        return
    sources = source_hashes(apps)
    worker = subprocess.run(['node', '-e', SVG_WORKER], input=json.dumps({
        'root': str(ROOT), 'sharp': args.sharp_module, 'size': SIZE * AA,
        'apps': [a for a in apps if not isinstance(a['icon'], str)],
    }), text=True, capture_output=True, check=True)
    rendered = json.loads(worker.stdout)
    images = [render_icon(a, rendered['images']) for a in apps]
    verify_icons(apps, images)
    data = ftx_bytes(images) if args.write_ftx else None
    if data is not None:
        verify_ftx(data, images)
    require(source_hashes(apps) == sources, 'Source changed during render; retry')
    OUT.mkdir(parents=True, exist_ok=True)
    entries = []
    for i, (app, im) in enumerate(zip(apps, images)):
        path = OUT / f'{app["id"]}.png'
        im.save(path)
        entries.append({'index': i, 'id': app['id'], 'title': app['title'], 'file': path.name,
                        'source': app['icon'], 'development_filter': app.get('status') == 'development',
                        'pixel_art': bool(app.get('pixelIcon')), 'sha256': sha(path.read_bytes())})
    if data is not None:
        FTX.parent.mkdir(parents=True, exist_ok=True)
        FTX.write_bytes(data)
    contact_sheet(apps, images).save(OUT / 'contact-sheet.png')
    manifest = {'size': SIZE, 'radius': RADIUS, 'supersample': AA, 'count': COUNT,
                'order': 'catalog/forge-store.json apps array (not UI sort)',
                'source_sha256': sources, 'renderer': {'pillow': PIL_VERSION, 'numpy': np.__version__,
                                                     'sharp': rendered['versions']},
                'ftx': None if data is None else {'path': str(FTX.relative_to(ROOT)), 'sha256': sha(data),
                                                'size_bytes': len(data), 'texture': [256, 256],
                                                'format': 'FTX1 RGBA8', 'raw_bytes': 262144},
                'icons': entries}
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    report = '''# Offline Forge home icons

23 RGBA PNGs, 44x44 pixels, circular radius 10. Source composition: 10 local
image icons and 13 original web SVG glyphs/gradients. Four development icons
(sm64ds, rvc, worms, dusklight) use saturate(.15), brightness(.86), contrast(.92).
Mutiny uses nearest sampling; other raster artwork uses Lanczos. Rounded clips
and the 1 px inset rim/top highlight use 8x area coverage. Original assets and
the older icon sets are inputs only. contact-sheet.png shows 1x and nearest 2x.

## Verification

The generator checks count, unique IDs, source/native catalog order, dimensions,
RGBA mode, symmetric alpha, transparent corners and the radius-10 quarter-circle
area. FTX validation independently decodes every Morton texel and compares all
23 ordered images plus their duplicated 1 px gutters. --check is read-only and
also checks source/output SHA-256. manifest.json records renderer versions.

## Native integration (not implemented by this tool)

1. catalog.c: add 44 to ICON_PX, expand ICON_SZ and both four-entry loops
   (catalog_load and app_icon_px). Existing loading uses icons-%d.ftx and crisp
   sampling; the sheet index remains App.icon. Update catalog.h's size comment.
   The 256x256 RGBA8 sheet adds 256 KiB of decoded texture memory when loaded.
2. Home's icon draw calls must request app_icon_px(app, 44) and draw at 1:1.
   Keep the selection frame outside the texture: 52x52, 2 px free gap, 2 px stroke.
   Without the loader change a 44 request still falls back to the 48 px sheet.
3. pack.py now includes icons-44 from these generated PNGs in its full asset
   pack. To refresh only this sheet, run this generator with --write-ftx.
   No full asset pack or application packaging was run during this change.
4. This is the 23-app Forge sheet only. Universal icons remain on their existing
   sheets and take the separate app_icon path; native 44 px Universal art is pending.

## Limits

No browser, emulator, hardware test, native build or application packaging was run.
Pillow/Lanczos and Sharp/librsvg rasterization are not proven byte-identical to
Chromium. Pixelated CSS downsampling can differ from nearest at non-integer ratios.
CSS outer drop shadow, selection and dynamic status overlays are not baked into
the strict 44x44 rounded tile; render them outside it if required. The underlying
parent inset shadow is covered by opaque artwork; the visible ::after rim is baked.
RVC has alpha 254: its background is composited against the light surface #eef2f4;
the dark web surface can differ by at most one RGB level from this single sheet.
Style constants mirror the reviewed CSS; CSS changes require reviewing this tool.
No claim of exact browser parity or console validation is made.

## Exact sheet order (zero-based)

| Index | Catalog ID | PNG |
| --- | --- | --- |
'''
    report += ''.join(f'| {e["index"]} | {e["id"]} | {e["file"]} |\n' for e in entries)
    report += '\n## Reproduce\n\n```sh\n'
    report += '3ds/tools/.venv/bin/python -I 3ds/tools/render-home-icons.py --write-ftx'
    if args.sharp_module != 'sharp':
        report += ' --sharp-module ' + shlex.quote(args.sharp_module)
    report += '\n'
    report += '3ds/tools/.venv/bin/python -I 3ds/tools/render-home-icons.py --check --write-ftx\n```\n'
    report += '\nIf Sharp is not resolvable, pass --sharp-module /absolute/path/to/installed/sharp. '
    report += 'The generator does not install dependencies or invoke pack.py.\n'
    (OUT / 'report.md').write_text(report)
    check(apps, args.write_ftx)
    print(f'Generated and verified {COUNT} icons in {OUT.relative_to(ROOT)}')
    if data is not None:
        print(f'{FTX.relative_to(ROOT)}: {len(data)} bytes; 256x256 RGBA8 (256 KiB decoded)')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(f'SVG renderer failed: {exc.stderr.strip()}', file=sys.stderr)
        sys.exit(1)
    except (OSError, ValueError, KeyError) as exc:
        print(f'Home icon generation failed: {exc}', file=sys.stderr)
        sys.exit(1)
