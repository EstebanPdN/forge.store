#!/usr/bin/env python3
"""Validate FNT3 metrics, Unicode coverage and every packed A8 pixel offline.
Also writes a 1:1 specimen rendered from the actual binary font atlas.
Usage: tools/.venv/bin/python -I tools/check-fonts.py [project-root]
"""
import base64
import hashlib
import json
import math
import struct
import sys
import zlib
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
GEN, GFX = ROOT/'3ds/build/gen', ROOT/'3ds/romfs/gfx'
source = json.loads((GEN/'glyphs.json').read_text())
data = (GFX/'fonts.bin').read_bytes()
magic, ns, npages, ng = struct.unpack_from('<4sHHI',data)
assert magic == b'FNT3' and ns == len(source['styles']) and npages <= 16
styles = [struct.unpack_from('<BBHffffII',data,12+28*i) for i in range(ns)]
off = 12+ns*28
glyphs = [struct.unpack_from('<IHHBBbbHBB',data,off+16*i) for i in range(ng)]
off += ng*16
ni, = struct.unpack_from('<H',data,off); off+=2
names=[]
for i in range(ni):
 n=data[off];off+=1;names.append(data[off:off+n].decode());off+=n
assert off == len(data) and names == source['icons'] and len(names)<=64
pages=[]
for i in range(npages):
 b=(GFX/f'glyphs-{i}.ftx').read_bytes()
 magic,w,h,fmt,count,_=struct.unpack_from('<4sHHBBH',b)
 rawsize,zsize=struct.unpack_from('<II',b,12)
 raw=zlib.decompress(b[20:])
 assert magic==b'FTX1' and fmt==3 and count==0 and rawsize==len(raw)==w*h and zsize==len(b)-20
 y,x=np.mgrid[0:h,0:w]
 m=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3)
 idx=(((y>>3)*(w>>3)+(x>>3))<<6)+m
 pages.append(np.frombuffer(raw,np.uint8)[idx])
lookup={}
for st,src in zip(styles,source['styles']):
 kind,fam,weight,size,asc,desc,line,first,count=st
 assert count==len(src['glyphs']) and first+count<=ng
 assert line == src['line'] and size==src['size'] and weight==src['weight']
 last=-1
 for g,s in zip(glyphs[first:first+count],sorted(src['glyphs'],key=lambda v:v['cp'])):
  cp,x,y,w,h,ox,oy,adv,page,insets=g
  assert cp>last and cp==s['cp'];last=cp
  assert (w,h,ox,oy,adv)==(s['w'],s['h'],s['ox'],s['oy'],round(s['adv']*64))
  assert page<npages and x+w<=pages[page].shape[1] and y+h<=pages[page].shape[0]
  if w:
   assert pages[page][y:y+h,x:x+w].tobytes()==base64.b64decode(s['a'])
   columns=np.flatnonzero(np.any(pages[page][y:y+h,x:x+w]>=32,axis=0))
   expected=0
   if len(columns):
    left,right=int(columns[0]),w-1-int(columns[-1])
    if left<=15 and right<=15:expected=left|(right<<4)
   assert insets==expected, (cp,insets,expected)
  else: assert insets==0
 if kind==0:
  lookup[fam,size,weight]=(st,{g[0]:g for g in glyphs[first:first+count]})
  assert 0xfffd in lookup[fam,size,weight][1] and 63 in lookup[fam,size,weight][1]
for size,line in [(9,12),(10,14),(11,15)]:
 for weight in [400,500,600,700]: assert lookup[0,size,weight][0][6]==line
for size in [14,15]: assert lookup[0,size,700][0][6]==18
for weight in [400,750]: assert (1,10,weight) in lookup

# Validate the additional production kerning table against every generated pair.
kern=(GFX/'kerning.bin').read_bytes();magic,n=struct.unpack_from('<4sH',kern);assert magic==b'KRN1' and n==ns
at=6
pair_maps={}
for src in source['styles']:
 count,=struct.unpack_from('<I',kern,at);at+=4
 pairs=[list(struct.unpack_from('<IIh',kern,at+j*10)) for j in range(count)];at+=count*10
 assert pairs==src.get('kerning',[])
 pair_maps[src['family'],src['size'],src['weight']]={(a,b):v/64 for a,b,v in pairs}
assert at==len(kern)

# This composes raw packed pixels using the C runtime's per-glyph baseline/advance.
image=Image.new('RGB',(800,240))
def draw(text,x,y,size,weight,family,ink):
 st,gs=lookup[family,size,weight]
 baseline=math.floor((st[6]-st[4]-st[5])/2+st[4]+.5);pen=0;previous=0;right=None;word_gap=0
 pairs=pair_maps[family,size,weight]
 for ch in text:
  g=gs.get(ord(ch),gs[0xfffd]);cp,gx,gy,w,h,ox,oy,advance,page,insets=g
  pen+=pairs.get((previous,ord(ch)),0);origin=math.floor(pen+.5)
  if w:
   if advance and right is not None:
    delta=max(0,right+(word_gap or 1)-ox-(insets&15)-origin);pen+=delta;origin+=delta
   mask=Image.fromarray(pages[page][gy:gy+h,gx:gx+w])
   image.paste(ink,(x+origin+ox,y+baseline+oy),mask)
   right=max(right if right is not None else -10000,origin+ox+w-(insets>>4));word_gap=0
  advance/=64
  if ch in ' \u00a0':word_gap=math.ceil(size*.3);advance=max(advance,word_gap)
  pen+=advance;previous=ord(ch)
samples=[('Inter 500: Body text / 11 px / 15 px',11,500,0),('Settings · Downloads · CIA / 3DSX',10,600,0),('Metadata · 1.2 MB · Nov 10, 2026',9,500,0),('Regular 400 / Medium 500 / Bold 700',10,400,0),('Español: instalación · Français: télécharger',11,500,0),('Deutsch: Herunterladen · Português: aplicação',11,500,0),('Русский: настройки и загрузки',11,500,0),('日本語：設定・ダウンロード',11,500,0),('移除PSP SDL 1.2后端支持',11,500,0),('Detail heading 15 / 18',15,700,0),('forge.store',10,750,1),('Remote replacement: \U0010fffd',10,500,0)]
for panel,(bg,ink) in enumerate([('#eef2f4','#20353e'),('#26282c','#edf2f4')]):
 image.paste(bg,(panel*400,0,panel*400+400,240))
 for i,(text,size,weight,fam) in enumerate(samples):draw(text,panel*400+10,7+i*19,size,weight,fam,ink)
image.save(GEN/'font-specimen.png')
report={'status':'pass','styles':ns,'glyphs':ng,'pages':[list(p.shape[::-1]) for p in pages], 'verified':['all glyph pixels round-trip Morton/A8','required exact sizes/weights/line heights','all font styles have replacement and question mark','glyph ranges, sorted codepoints, atlas bounds, complete binary length'], 'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [GFX/'fonts.bin']+sorted(GFX.glob('glyphs-*.ftx'))}}
(GEN/'font-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
