#!/usr/bin/env python3
"""Bake the web's static install backdrop filters from the authorized local art.
Only this background is baked; native controls, progress, text and particles stay live.
"""
from pathlib import Path
import hashlib,json,runpy,sys
import numpy as np
from PIL import Image,ImageFilter,ImageEnhance,ImageOps
root=Path(__file__).resolve().parents[2]
pack=runpy.run_path(str(root/'3ds/tools/pack.py'),run_name='forge_pack')
out=root/'3ds/romfs/gfx/install';out.mkdir(exist_ok=True)
report=[]
for app in json.loads((root/'catalog/forge-store.json').read_text())['apps']:
 b=app.get('banner',{});art=b.get('bg') or b.get('mid')
 if not isinstance(art,str) or not (root/art).is_file():continue
 src=root/art;im=Image.open(src).convert('RGB')
 # .in-backdrop: inset -24, cover, blur 14px, saturation 1.25, brightness .55, scale 1.1.
 im=ImageOps.fit(im,(493,317),method=Image.Resampling.LANCZOS)
 im=ImageEnhance.Color(im.filter(ImageFilter.GaussianBlur(15.4))).enhance(1.25)
 im=ImageEnhance.Brightness(im).enhance(.55).crop((46,38,446,278))
 pixels=np.asarray(im,dtype=np.float32);y,x=np.mgrid[:240,:400]
 radial=np.maximum(0,1-np.sqrt(((x-200)/240)**2+((y-96)/168)**2)/.7)*.2
 pixels=pixels*(1-radial[:,:,None])+np.array([255,140,40])*radial[:,:,None]
 alpha=.25+.3*(y+.5)/240;pixels*=1-alpha[:,:,None]
 im=Image.fromarray(np.rint(pixels).clip(0,255).astype(np.uint8))
 dst=out/(app['id']+'.ftx');pack['write_sheet'](dst,[im],pack['RGB8'])
 report.append(dict(id=app['id'],source=art,source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),sha256=hashlib.sha256(dst.read_bytes()).hexdigest()))
(root/'3ds/build/gen/install-backdrops.json').write_text(json.dumps(report,indent=2)+'\n')
print('Baked',len(report),'install backgrounds')
