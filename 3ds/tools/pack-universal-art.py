"""Pack Universal-DB art without shrinking grid icons before drawing them at 44 px."""
from pathlib import Path
import json
import runpy
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageOps, ImageColor

ROOT = Path(__file__).resolve().parents[2]
ICONS_ONLY = '--icons-only' in sys.argv
sys.argv = ['pack.py', str(ROOT)]
p = runpy.run_path(str(ROOT / '3ds/tools/pack.py'))
apps = json.loads((ROOT / '3ds/romfs/universal.json').read_text())['apps']
# Matches catalog.c. 50 padded 44 px icons fit in 512x256 (256 KiB, RGBA4).
# 64 would require a 512x512 texture and waste scarce Old 3DS linear memory.
ICONS_PER_SHEET = 50
ICON_PX = 44

icons = []
for i, a in enumerate(apps):
    try:
        with Image.open(ROOT / f'3ds/build/universal-icons/{i}.img') as source:
            im = source.convert('RGBA')
        if im.size != (ICON_PX, ICON_PX):
            sampling = Image.Resampling.NEAREST if max(im.size) <= ICON_PX else Image.Resampling.LANCZOS
            im = im.resize((ICON_PX, ICON_PX), sampling)
    except (OSError, ValueError):
        im = Image.new('RGBA', (ICON_PX, ICON_PX), a.get('color', '#556070'))
        ImageDraw.Draw(im).rounded_rectangle((12, 12, 36, 36), 5, outline='white', width=3)
    # V2 clipping: 44px tile, circular radius10, antialiased coverage at8x.
    mask=Image.new('L',(ICON_PX*8,ICON_PX*8),0)
    ImageDraw.Draw(mask).rounded_rectangle((0,0,ICON_PX*8-1,ICON_PX*8-1),80,fill=255)
    mask=mask.resize((ICON_PX,ICON_PX),Image.Resampling.BOX)
    from PIL import ImageChops
    im.putalpha(ImageChops.multiply(im.getchannel('A'),mask))
    icons.append(im)
for i in range(0, len(icons), ICONS_PER_SHEET):
    p['write_sheet'](ROOT / f'3ds/romfs/gfx/universal-{i // ICONS_PER_SHEET}.ftx',
                     icons[i:i + ICONS_PER_SHEET], p['RGBA4'])
sheet_count = (len(icons) + ICONS_PER_SHEET - 1) // ICONS_PER_SHEET
for old in (ROOT / '3ds/romfs/gfx').glob('universal-*.ftx'):
    if int(old.stem.split('-')[-1]) >= sheet_count:
        old.unlink()

if ICONS_ONLY:
    report={'count':len(icons),'size':44,'radius':10,'format':'RGBA4','sheets':sheet_count,'transparentCorners':all(im.getpixel((0,0))[3]==0 for im in icons)}
    assert report['transparentCorners']
    (ROOT/'3ds/build/gen/universal-icons-v2.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report)); sys.exit(0)

# Native banners use an app's own artwork, loaded on demand.
out = ROOT / '3ds/romfs/gfx/universal'
out.mkdir(exist_ok=True)
for i, a in enumerate(apps):
    try:
        with Image.open(ROOT / f'3ds/build/universal-banners/{i}.img') as source:
            art = source.convert('RGBA')
        im = ImageOps.fit(art, (432, 272), Image.Resampling.NEAREST)
    except (OSError, ValueError):
        im = Image.new('RGBA', (432, 272), a.get('color', '#556070'))
    # Banner.compose's background includes this tint before its Ken Burns
    # transform. Keep it in the background layer, distinct from the detail shade.
    pix=np.asarray(im.convert('RGB'),dtype=np.float32)
    yy=np.arange(272,dtype=np.float32)[:,None]/272
    opacity=np.where(yy<.22,.22*(1-yy/.22),np.where(yy<.45,0,(yy-.45)/.55))*.88
    tint=np.array(ImageColor.getrgb(a.get('color','#556070')),dtype=np.float32)*.6
    color=np.where((yy<.22)[...,None],0,tint)
    pix=pix*(1-opacity[...,None])+color*opacity[...,None]
    im=Image.fromarray(np.rint(pix).clip(0,255).astype(np.uint8)).convert('RGBA')
    p['write_sheet'](out / (a['id'] + '.ftx'), [im], p['RGB565'])
print(len(apps), 'Universal icons/banners packed;', sheet_count, 'icon sheets at 44 px')
