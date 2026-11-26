#!/usr/bin/env python3
"""Re-render only the named apps' cells in every native icon atlas.

Uses render-home-icons.py's render_icon (same mask, rim and highlight) at each draw size,
keeps every other cell byte-identical, and needs no browser or Sharp. Only apps whose
catalog icon is an image file can be updated this way.
Usage: 3ds/tools/.venv/bin/python -I 3ds/tools/update-app-icons.py celeste3ds silenthill
"""
import importlib.util, json, struct, sys, zlib
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
def load(name, file, argv=None):
    old = sys.argv; sys.argv = argv or sys.argv
    spec = importlib.util.spec_from_file_location(name, ROOT / '3ds/tools' / file)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    sys.argv = old; return module
pack = load('forge_pack', 'pack.py', ['pack.py', str(ROOT)])
icons = load('home_icons', 'render-home-icons.py')

def read_sheet(path):
    data = path.read_bytes()
    magic, w, h, fmt, n, _ = struct.unpack_from('<4sHHBBH', data)
    assert magic == b'FTX1' and fmt == pack.RGBA8, path
    rects = [struct.unpack_from('<HHHH', data, 12 + 8 * i) for i in range(n)]
    offset = 12 + 8 * n
    _, zlen = struct.unpack_from('<II', data, offset)
    px = np.frombuffer(zlib.decompress(data[offset + 8:offset + 8 + zlen]), np.uint8).reshape(-1, 4)
    canvas = px[pack.morton_index(w, h)][:, [3, 2, 1, 0]].reshape(h, w, 4)
    return [Image.fromarray(canvas[y:y + ch, x:x + cw].copy()) for x, y, cw, ch in rects]

apps = json.loads((ROOT / 'catalog/forge-store.json').read_text())['apps']
ids = [a['id'] for a in apps]
targets = sys.argv[1:]
assert targets and all(t in ids and isinstance(apps[ids.index(t)]['icon'], str) for t in targets), 'unknown app or non-image icon'
for size in (20, 32, 40, 44, 48, 64):
    path = ROOT / '3ds/romfs/gfx' / ('icons.ftx' if size == 48 else f'icons-{size}.ftx')
    cells = read_sheet(path)
    assert len(cells) == len(apps), f'{path.name}: {len(cells)} cells for {len(apps)} apps'
    icons.SIZE, icons.RADIUS = size, round(size * 10 / 44)
    for t in targets:
        cells[ids.index(t)] = icons.render_icon(apps[ids.index(t)], {})
    before = [np.asarray(c).copy() for c in cells]
    pack.write_sheet(path, cells, pack.RGBA8)
    after = read_sheet(path)
    assert all(np.array_equal(np.asarray(a), b) for a, b in zip(after, before)), f'{path.name}: round trip differs'
    print(f'{path.relative_to(ROOT)}: {", ".join(targets)} at {size} px')
