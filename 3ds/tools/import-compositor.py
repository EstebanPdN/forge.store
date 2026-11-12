#!/usr/bin/env python3
"""Extract actual 1× screen regions from Code Atlas compositor PNGs.

The compositor carries its own ICC profile. Convert that declared color space
into sRGB with LittleCMS; never fit colors, move content, or resample pixels.
Original full viewport PNGs and crop coordinates are retained for review.
"""
from pathlib import Path
from PIL import Image,ImageCms
from io import BytesIO
import argparse,json,hashlib,shutil
ap=argparse.ArgumentParser();ap.add_argument('manifest',type=Path);ap.add_argument('out',type=Path);args=ap.parse_args()
args.out.mkdir(parents=True,exist_ok=True);raw=args.out/'viewport-originals';raw.mkdir(exist_ok=True)
for capture in json.loads(args.manifest.read_text()):
 src=Path(capture['screenshotPath']);image=Image.open(src)
 expected=(capture['viewport']['width'],capture['viewport']['height'])
 assert image.size==expected,(image.size,expected)
 original=raw/(capture['output']+'.png');shutil.copyfile(src,original)
 profile=image.info.get('icc_profile');description=None
 if profile:
  declared=ImageCms.ImageCmsProfile(BytesIO(profile));description=ImageCms.getProfileDescription(declared).strip()
  image=ImageCms.profileToProfile(image,declared,ImageCms.createProfile('sRGB'),outputMode='RGB')
 else:image=image.convert('RGB')
 for screen,width in [('top',400),('bottom',320)]:
  box=capture['rects'][screen];assert box['width']==width and box['height']==240
  assert all(isinstance(box[k],int) for k in box)
  x,y=box['x'],box['y'];assert x>=0 and y>=0 and x+width<=image.width and y+240<=image.height
  image.crop((x,y,x+width,y+240)).save(args.out/(capture['output']+'-'+screen+'.png'))
 capture['sourceICC']=description;capture['originalViewport']=str(original.resolve());capture['originalViewportSha256']=hashlib.sha256(src.read_bytes()).hexdigest()
 capture['colorConversion']='Declared embedded ICC to sRGB, LittleCMS relative colorimetric; no fitted correction' if profile else 'Already untagged RGB; retained values'
 (args.out/(capture['output']+'.state.json')).write_text(json.dumps(capture,indent=2)+'\n')
 print(capture['output'],description or 'untagged RGB')
