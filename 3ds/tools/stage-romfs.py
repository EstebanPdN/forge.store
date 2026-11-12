#!/usr/bin/env python3
"""Stage RomFS with one sequential banner bank instead of dozens of small reads.

The checked-in FTX inputs remain the source of truth. Payloads are byte-identical;
the executable keeps their compressed bank in normal RAM and bounds GPU residency.
"""
import importlib.util,json,shutil,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
source=ROOT/'3ds/romfs';target=ROOT/'3ds/build/romfs'
if target.exists():shutil.rmtree(target)
shutil.copytree(source,target,ignore=shutil.ignore_patterns('banner'))
apps=json.loads((source/'catalog.json').read_text())['apps']
files=[]
for app in apps:
 for layer in ('bg','fg'):
  p=source/'gfx/banner'/f'{app["id"]}-{layer}.ftx'
  if p.exists():files.append(p)
start=12+72*len(files);index=bytearray();payload=bytearray()
meta=json.loads((target/'gfx/banners.json').read_text())
for app in apps:
 meta.setdefault(app['id'],{})['_bytes']=0
for p in files:
 data=p.read_bytes();name=p.name.encode();assert len(name)<64
 index.extend(struct.pack('<64sII',name,start+len(payload),len(data)));payload.extend(data)
 at=12+data[9]*8
 app_id=p.stem.rsplit('-',1)[0];meta[app_id]['_bytes']+=struct.unpack_from('<I',data,at)[0]
bank=struct.pack('<4sII',b'FBN1',len(files),start+len(payload))+index+payload
assert len(bank)<=8*1024*1024
(target/'gfx/banner-bank.bin').write_bytes(bank)
(target/'gfx/banners.json').write_text(json.dumps(meta,separators=(',',':'))+'\n')
print(f'Banner bank: {len(files)} sheets, {len(bank)} bytes, lossless / no duplicate FTX payloads')
spec=importlib.util.spec_from_file_location('music_pack',ROOT/'3ds/tools/music-pack.py')
music_pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(music_pack)
for name in ('music-start','music'):
 p=target/'audio'/f'{name}.pcm'
 original=p.read_bytes();packed=music_pack.encode(original)
 p.with_suffix('.fpcm.gz').write_bytes(packed);p.unlink()
 print(f'{name}: {len(original)} → {len(packed)} bytes, sample-identical PCM')
