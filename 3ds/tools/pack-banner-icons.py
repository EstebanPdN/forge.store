#!/usr/bin/env python3
"""Bake official icons and generated backgrounds at native resolution.

Foreground tilt and shadow are authored once, never re-filtered by an idle animation.
Only generated backgrounds are rebuilt; authored game scenery is preserved.
"""
from pathlib import Path
import importlib.util,json,math
import numpy as np
from PIL import Image,ImageDraw,ImageChops,ImageFilter,ImageOps
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('forge_pack',ROOT/'3ds/tools/pack.py')
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
catalog=json.loads((ROOT/'catalog/forge-store.json').read_text())
path=ROOT/'3ds/romfs/gfx/banners.json';meta=json.loads(path.read_text());count=0

def rgb(value):
    return np.array([int(value[i:i+2],16) for i in (1,3,5)],dtype=float)

def background(app):
    """The CSS linear/radial gradients, fading dot grid and lower tint."""
    w,h=432,254
    y,x=np.mgrid[0:h,0:w].astype(float);x+=.5;y+=.5
    if app['banner'].get('bg'):
        im=Image.open(ROOT/app['banner']['bg']).convert('RGB')
        pixels=np.asarray(ImageOps.fit(im,(w,h),Image.Resampling.LANCZOS),dtype=float)
        # Keep the wordmark readable over busy gameplay, without hiding the art.
        shade=(.30*np.clip(1-x/(w*.7),0,1))[...,None]
        pixels=pixels*(1-shade)
    else:
        c1,c2,c3=map(rgb,app['banner']['gradient'])
        sx,sy=math.sin(math.radians(160)),-math.cos(math.radians(160))
        t=np.clip(((x-w/2)*sx+(y-h/2)*sy)/(w*sx+h*sy)+.5,0,1)[...,None]
        pixels=c2*(1-t)+c1*t
        radius=np.sqrt(((x-.75*w)/(1.2*w))**2+((y-.15*h)/(.9*h))**2)
        a=(.4*np.clip(1-radius/.6,0,1))[...,None]
        pixels=pixels*(1-a)+c3*a
        distance=np.sqrt(((x%8)-4)**2+((y%8)-4)**2)
        a=(.16*np.clip((1.1-distance)/.3,0,1)*np.clip(1-y/(.85*h),0,1))[...,None]
        pixels=pixels*(1-a)+255*a
    top=(.22*.88*np.clip(1-y/(.22*h),0,1))[...,None]
    bottom=(.88*np.clip((y/h-.45)/.55,0,1))[...,None]
    pixels=pixels*(1-top)
    pixels=pixels*(1-bottom)+rgb(app.get('color','#333333'))*.6*bottom
    return Image.fromarray(np.uint8(np.clip(pixels,0,255))).convert('RGBA')

for app in catalog['apps']:
    banner=app.get('banner',{})
    if banner.get('cardIcon') or (not banner.get('bg') and banner.get('gradient')) or app['id']=='minish':
        pack.write_sheet(ROOT/f'3ds/romfs/gfx/banner/{app["id"]}-bg.ftx',[background(app)],pack.RGB565)
    if (banner.get('bg') and not banner.get('cardIcon')) or banner.get('mid') or not isinstance(app.get('icon'),str):continue
    # Existing authored logos continue to own their entire composition.
    if banner.get('logo') and not banner['logo'].startswith('assets/community/'):continue
    source=Image.open(ROOT/banner.get('heroIcon',app['icon'])).convert('RGBA')
    sampling=Image.Resampling.NEAREST if app.get('pixelIcon') else Image.Resampling.LANCZOS
    image=source.resize((456,456),sampling)
    tint=tuple(np.uint8(rgb(app['iconBackground']) if app.get('iconBackground') else rgb(app.get('color','#333333'))*.32+rgb('#172334')*.68))+(255,)
    backing=Image.new('RGBA',image.size,tint);backing.alpha_composite(image);image=backing
    mask=Image.new('L',image.size);ImageDraw.Draw(mask).rounded_rectangle((0,0,455,455),radius=96,fill=255)
    image.putalpha(ImageChops.multiply(image.getchannel('A'),mask))
    # Static 8-degree tilt, supersampled once. The resulting sprite is drawn 1:1.
    image=image.rotate(-8,Image.Resampling.BICUBIC,expand=True)
    layer=Image.new('RGBA',(696,696));left=(696-image.width)//2;top=(696-image.height)//2
    shadow=Image.new('L',layer.size);shadow.paste(image.getchannel('A'),(left,top+56))
    shadow=shadow.filter(ImageFilter.GaussianBlur(40)).point(lambda a:round(a*.4))
    layer.putalpha(shadow);layer.alpha_composite(image,(left,top))
    image=layer.resize((174,174),Image.Resampling.LANCZOS)
    frames=[image];layers={'mid':{'i':0,'x':226,'y':10,'w':174,'h':174}}
    if banner.get('logo'):
        logo=Image.open(ROOT/banner['logo']).convert('RGBA')
        logo.thumbnail((200,96),Image.Resampling.LANCZOS)
        frames.append(logo);layers['logo']={'i':1,'x':22,'y':92-logo.height//2,'w':logo.width,'h':logo.height}
    pack.write_sheet(ROOT/f'3ds/romfs/gfx/banner/{app["id"]}-fg.ftx',frames,pack.RGBA8)
    meta[app['id']]=layers
    count+=1
path.write_text(json.dumps(meta,ensure_ascii=False)+'\n')
print(f'Packed {count} official banner icons; logos share their app’s foreground atlas.')
