#!/usr/bin/env python3
"""Build the five curated 15-cover grids from recorded, hash-checked sources.

Authoring only: Python/Pillow; normal application builds use the checked-in WebP.
Downloaded originals stay in .local and never become runtime dependencies.
"""
from pathlib import Path
import hashlib,json,urllib.request
from PIL import Image,ImageOps,ImageFilter
ROOT=Path(__file__).resolve().parents[1]
CACHE=ROOT/'.local/cover-sources';CACHE.mkdir(parents=True,exist_ok=True)
records=json.loads((ROOT/'assets/community/cover-sources.json').read_text())
for app in sorted({r['app'] for r in records}):
    covers=[r for r in records if r['app']==app]
    assert len(covers)==15 and len({r['sha256'] for r in covers})==15
    canvas=Image.new('RGB',(1000,600),'#141820')
    for i,r in enumerate(covers):
        path=CACHE/(r['sha256']+'.png')
        if not path.exists():
            with urllib.request.urlopen(r['url'],timeout=30) as response:path.write_bytes(response.read())
        assert hashlib.sha256(path.read_bytes()).hexdigest()==r['sha256'],r['title']
        tile=ImageOps.fit(Image.open(path).convert('RGB'),(200,200),Image.Resampling.LANCZOS)
        canvas.paste(tile,((i%5)*200,(i//5)*200))
    # The blur is baked once, rather than spending GPU time on every frame.
    canvas.filter(ImageFilter.GaussianBlur(2.2)).resize((800,480),Image.Resampling.LANCZOS).save(ROOT/f'assets/community/{app}/background.webp',quality=91)
    print(app,': 15 distinct covers')
