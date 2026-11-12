#!/usr/bin/env python3
"""Offline FreeType grayscale glyphs + Cairo SVG icons, at native pixel sizes.

Use render-fonts.mjs to refresh language and SVG source inputs first. This script
writes only build/gen. pack.py --fonts-only installs the FNT3 atlas into romfs/gfx.
FORGE_FALLBACK_FONTS can override the colon-separated local fallback font paths.
Fractional metrics and pair kerning are baked: the C runtime advances one Unicode scalar at a time.
"""
import base64
import hashlib
import io
import json
import math
import os
import re
import sys
import unicodedata
from functools import lru_cache
from pathlib import Path

import cairosvg
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont, features

ROOT = Path(sys.argv[1]).resolve()
GEN = ROOT / '3ds/build/gen'
INTER = ROOT / 'assets/fonts/inter/Inter-var.ttf'
MONO = [ROOT / 'assets/fonts/space-mono' / f for f in ['SpaceMono-Regular.ttf','SpaceMono-Bold.ttf']]
DM = [ROOT / 'assets/fonts' / f for f in ['468d56b6b25b05b70190.woff2', '219b02c7d8884817d3d6.woff2']]
DEFAULT_FALLBACK = ['/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc', '/System/Library/Fonts/Hiragino Sans GB.ttc', '/System/Library/Fonts/Apple Symbols.ttf', '/System/Library/Fonts/Apple Color Emoji.ttc', '/System/Library/Fonts/ArialHB.ttc']
FALLBACK = [Path(p) for p in os.environ.get('FORGE_FALLBACK_FONTS', ':'.join(DEFAULT_FALLBACK)).split(':') if p and Path(p).exists()]
SOURCES = [GEN / 'font-strings.json', ROOT / 'catalog/forge-store.json', ROOT / '3ds/romfs/universal.json']
SOURCES += sorted((ROOT / '3ds/source').glob('*.c')) + sorted((ROOT / '3ds/source').glob('*.h')) + sorted((ROOT / '3ds/source').glob('*.inc'))
chars = set(chr(i) for i in range(32, 127)) | set('\u00a0\ufffd')

def walk(value):
    if isinstance(value, str):
        chars.update(value)
    elif isinstance(value, list):
        for v in value: walk(v)
    elif isinstance(value, dict):
        for v in value.values(): walk(v)

for path in SOURCES:
    if path.suffix == '.json': walk(json.loads(path.read_text()))
    else:
        source = path.read_text()
        # Search normalization mappings are lookup data, not displayed text.
        source = re.sub(r'static const struct \{ u32 code; const char \*text; \} search_fold\[\] = \{.*?\n\};', '', source, flags=re.S)
        chars.update(source)
chars.update(''.join(chars).upper())
# Control/format codepoints have no independent ink; keep combining marks.
ALL = sorted(ord(c) for c in chars if not unicodedata.category(c).startswith('C') or c == '\ufffd')
strings = json.loads((GEN / 'font-strings.json').read_text())
brand = set(chr(i) for i in range(32, 127)) | set('\u00a0\ufffd’')
for lang in strings.values():
    for key in ['settings.title', 'downloads.title', 'search.title', 'sd.title', 'picker.titleA', 'picker.titleB']:
        brand.update(lang.get(key, ''))
BRAND = sorted(ord(c) for c in brand)

@lru_cache(None)
def cmap(path):
    with TTFont(path, fontNumber=0) as font: return set(font.getBestCmap())

@lru_cache(None)
def face(path, size, weight):
    f = ImageFont.truetype(str(path), size, layout_engine=ImageFont.Layout.RAQM)
    try:
        axes = f.get_variation_axes()
        f.set_variation_by_axes([min(a['maximum'], max(a['minimum'], weight if a['name'] == b'Weight' else size if a['name'] == b'Optical size' else a['default'])) for a in axes])
    except OSError: pass  # static fallback font
    return f

fallback_usage = {}
missing = set()

def select(cp, family, weight=500):
    paths = ([INTER] if family == 0 else [MONO[weight>=600],INTER] if family == 2 else DM + [INTER]) + FALLBACK
    for path in paths:
        if cp in cmap(path):
            if path != paths[0]: fallback_usage.setdefault(str(path), set()).add(cp)
            return path, cp
    missing.add(cp)
    # Known unsupported characters explicitly share the replacement glyph.
    return INTER, 0xfffd if 0xfffd in cmap(INTER) else ord('?')

def raster(cp, family, size, weight):
    if 0xfe00 <= cp <= 0xfe0f:
        return dict(cp=cp,adv=0,ox=0,oy=0,w=0,h=0,a='')
    path, actual = select(cp, family, weight)
    emoji = path.name == 'Apple Color Emoji.ttc'
    scale = size / 32 if emoji else 1
    f = face(path, 32 if emoji else size, weight)
    ch = chr(actual)
    bounds = f.getbbox(ch, anchor='ls')
    x0, y0, x1, y1 = bounds
    mask = Image.new('RGBA' if emoji else 'L', (max(1, x1-x0), max(1, y1-y0)))
    ImageDraw.Draw(mask).text((-x0, -y0), ch, font=f, fill='white' if emoji else 255, anchor='ls', embedded_color=emoji)
    if emoji:
        mask = mask.getchannel('A').resize((max(1,round(mask.width*scale)),max(1,round(mask.height*scale))),Image.Resampling.BOX)
        x0, y0 = round(x0*scale), round(y0*scale)
    crop = mask.getbbox()
    glyph = dict(cp=cp, adv=f.getlength(ch)*scale, ox=0, oy=0, w=0, h=0, a='')
    if crop:
        mask = mask.crop(crop)
        glyph.update(ox=x0+crop[0], oy=y0+crop[1], w=mask.width, h=mask.height, a=base64.b64encode(mask.tobytes()).decode())
    return glyph

def line_height(size):
    return {8:11, 9:12, 10:14, 11:15, 12:16, 13:17, 14:18, 15:18, 30:30}.get(size, math.ceil(size*1.2))

# Kerning candidates include every ASCII pair plus adjacent scalars in the source corpus.
pairs={(a,b) for a in range(32,127) for b in range(32,127)}
for path in SOURCES:
    text=path.read_text()
    pairs.update((ord(a),ord(b)) for a,b in zip(text,text[1:]) if ord(a) in ALL and ord(b) in ALL)
styles = []
# V2 native scale, including explicit regular/medium/semibold/bold small text.
for size in [8,9,10,11,12,13,14,15,20,21,22]:
    for weight in ([400,500,600,700,800,900] if size in [9,10,11] else [500,600,700,800,900]):
        styles.append((0, size, weight, ALL))
for size in [9,10,11,15,20,21,23]:
    for weight in [400,750]: styles.append((1,size,weight,BRAND))
styles.append((1,22,800,BRAND))
styles.extend([(2,10,400,ALL),(2,10,600,ALL)])
styles.append((1,30,800,ALL)) # Banner wordmarks when an app has no logo image.
out = {'version':3, 'rasterizer':'Pillow/FreeType native grayscale', 'coverageContrast':1, 'styles':[], 'icons':[]}
for family, size, weight, cps in styles:
    primary = face(INTER if family == 0 else MONO[weight>=600] if family == 2 else DM[0],size,weight)
    ascent, descent = primary.getmetrics()
    glyphs = [raster(cp, family, size, weight) for cp in cps]
    advances={g['cp']:g['adv'] for g in glyphs}
    kern=[]
    for a,b in sorted(pairs):
        if a not in advances or b not in advances: continue
        pa,aa=select(a,family,weight);pb,bb=select(b,family,weight)
        if pa!=pb or aa!=a or bb!=b or pa.name=='Apple Color Emoji.ttc': continue
        f=face(pa,size,weight)
        delta=round((f.getlength(chr(a)+chr(b),features=['-liga','-clig'])-advances[a]-advances[b])*64)
        if delta: kern.append([a,b,delta])
    out['styles'].append(dict(kind='font', family=family, size=size, weight=weight, ascent=ascent, descent=descent, line=line_height(size), glyphs=glyphs,kerning=kern))
    print(f'{"Inter" if family == 0 else "Space Mono" if family == 2 else "DM Sans"} {weight} {size}/{line_height(size)}: {len(glyphs)} glyphs', flush=True)

# Exact existing SVG strokes, rasterized locally. Include all source names, not an old subset.
icons = json.loads((GEN / 'font-ui-icons.json').read_text())
icons['starFill'] = icons['star']
out['icons'] = list(icons)
for size in [8,9,10,11,12,13,14,16,18,20,26]:
    glyphs = []
    for cp, (name, body) in enumerate(icons.items()):
        fill = '#fff' if name == 'starFill' else 'none'
        svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 24 24" color="#fff" fill="{fill}" stroke="#fff" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">{body}</svg>'
        png = cairosvg.svg2png(bytestring=svg.encode())
        alpha = Image.open(io.BytesIO(png)).convert('RGBA').getchannel('A')
        # Two transparent pixels preserve the original icon metric convention.
        mask = Image.new('L', (size+4,size+4)); mask.paste(alpha,(2,2))
        glyphs.append(dict(cp=cp,adv=size+4,ox=-2,oy=-2,w=size+4,h=size+4,a=base64.b64encode(mask.tobytes()).decode()))
    out['styles'].append(dict(kind='icon',family=0,size=size,weight=0,ascent=size,descent=0,line=size,glyphs=glyphs))
(GEN / 'glyphs.json').write_text(json.dumps(out,separators=(',',':')))
report = dict(rasterizer=out['rasterizer'],freetype=features.version('freetype2'),languages=sorted(strings),codepoints=len(ALL),styles=len(out['styles']),icons=len(icons),inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCES},fonts={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [INTER]+DM+MONO+FALLBACK},fallback={p:len(v) for p,v in fallback_usage.items()},replacements=[f'U+{cp:04X}' for cp in sorted(missing)],notes=['Glyph-by-glyph layout; pair kerning; no contextual shaping, bidi or emoji sequences.', 'Remote scalars outside baked coverage use U+FFFD then question mark at runtime.', 'Local system fallback font paths may need FORGE_FALLBACK_FONTS on another machine.'])
(GEN / 'font-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['languages','codepoints','styles','icons','fallback','replacements']}))
