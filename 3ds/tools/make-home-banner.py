#!/usr/bin/env python3
"""Author the small HOME banner from shared brand assets; no runtime rendering tools.
Requires Pillow, fonttools, gltflib and a pinned pycgfx checkout (see docs/HOME-BANNER.md).
"""
import argparse, hashlib, importlib.util, json, math, struct, subprocess, sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont
from home_banner_validate import inspect, unpack_banner
ROOT = Path(__file__).resolve().parents[2]
PIN = '6c2e0cd53a5f4c3265b9aa9a1f30806f8291116e'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pycgfx',type=Path,required=True)
    ap.add_argument('--out',type=Path,default=ROOT/'3ds/build/home-banner/generated')
    a=ap.parse_args(); out=a.out.resolve(); out.mkdir(parents=True,exist_ok=True)
    revision=subprocess.check_output(['git','-C',str(a.pycgfx),'rev-parse','HEAD'],text=True).strip()
    if revision != PIN: raise ValueError('Unexpected pycgfx revision: '+revision)
    for source,name in [('468d56b6b25b05b70190.woff2','brand'),('20fc9b6fc618e7c3c68d.woff2','ui')]:
        font=TTFont(ROOT/'assets/fonts'/source);font.flavor=None;font.save(out/(name+'.ttf'))
    # One precomputed alpha oval, never a runtime blur. RGB remains valid even at alpha zero.
    bg=Image.new('RGBA',(256,128)); px=bg.load()
    for y in range(128):
        for x in range(256):
            r=math.hypot((x-127.5)/126,(y-63.5)/62)
            t=max(0,min(1,(1-r)/.65)); alpha=round(255*t*t*(3-2*t))
            mix=(x/255+y/127)/2
            col=[round(240+5*mix)]*3
            if (x%7==3 and y%7==3): col=[round(v*.84) for v in col]
            px[x,y]=(*col,alpha)
    bg.save(out/'oval.png')
    # Rasterize the wordmark once, with a separate subtitle baseline. Preserve the fonts.
    text=Image.new('RGBA',(512,128)); d=ImageDraw.Draw(text)
    bold=ImageFont.truetype(str(out/'brand.ttf'),88);bold.set_variation_by_axes([800])
    regular=ImageFont.truetype(str(out/'brand.ttf'),88);regular.set_variation_by_axes([400])
    small=ImageFont.truetype(str(out/'ui.ttf'),28);small.set_variation_by_axes([800])
    d.text((0,66),'forge.',font=bold,fill='#dd741b',anchor='ls')
    d.text((round(d.textlength('forge.',font=bold)),66),'store',font=regular,fill='#2c3e45',anchor='ls')
    d.text((4,119),'Ports for Nintendo 3DS',font=small,fill='#465960',anchor='ls')
    text=text.point(lambda v: (v//16)*17)
    text.save(out/'wordmark.png')
    Image.open(ROOT/'assets/brand/forge-icon.png').resize((128,128),Image.Resampling.NEAREST).save(out/'logo.png')
    g=dict(asset={'version':'2.0','generator':'Forge HOME banner'},scene=0,scenes=[{'nodes':[]}],nodes=[],meshes=[],materials=[],textures=[],images=[],samplers=[{'magFilter':9729,'minFilter':9729,'wrapS':33071,'wrapT':33071}],buffers=[],bufferViews=[],accessors=[],animations=[],extensionsUsed=['KHR_materials_unlit'])
    buf=bytearray()
    def access(values,kind,ctype=5126):
        while len(buf)%4:buf.append(0)
        n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[kind]; start=len(buf)
        flat=[v for row in values for v in (row if isinstance(row,(tuple,list)) else [row])]
        buf.extend(struct.pack('<'+('f' if ctype==5126 else 'H')*len(flat),*flat))
        g['bufferViews'].append(dict(buffer=0,byteOffset=start,byteLength=len(buf)-start))
        a=dict(bufferView=len(g['bufferViews'])-1,componentType=ctype,count=len(values),type=kind)
        if kind in ('SCALAR','VEC3'):
            rows=[flat[i:i+n] for i in range(0,len(flat),n)];a['min']=[min(r[i] for r in rows) for i in range(n)];a['max']=[max(r[i] for r in rows) for i in range(n)]
        g['accessors'].append(a);return len(g['accessors'])-1
    def mat(name,rgba=(1,1,1,1),image=None,alpha=False):
        p=dict(baseColorFactor=list(rgba),metallicFactor=0,roughnessFactor=1)
        if image:
            g['images'].append(dict(uri=image));g['textures'].append(dict(source=len(g['images'])-1,sampler=0));p['baseColorTexture']={'index':len(g['textures'])-1}
        g['materials'].append(dict(name=name,pbrMetallicRoughness=p,alphaMode='BLEND' if alpha else 'OPAQUE',doubleSided=False,extensions={'KHR_materials_unlit':{}}));return len(g['materials'])-1
    paper=mat('Oval',image='oval.png',alpha=True); lettering=mat('Wordmark',image='wordmark.png',alpha=True)
    rim=mat('Porcelain',(1,1,1,1));edge=mat('Edge',(.68,.73,.76,1));face=mat('Logo',image='logo.png')
    def primitive(p,n,uv,idx,m,local=False):
        p=[(x*1.05,y*1.05 if local else 1+(y-1)*1.05,z*1.05) for x,y,z in p]
        attrs=dict(POSITION=access(p,'VEC3'),NORMAL=access(n,'VEC3'))
        if uv:attrs['TEXCOORD_0']=access(uv,'VEC2')
        return dict(attributes=attrs,indices=access(idx,'SCALAR',5123),material=m,mode=4)
    def quad(x,y,z,w,h,m):
        return primitive([(x,y,z),(x+w,y,z),(x+w,y+h,z),(x,y+h,z)],[(0,0,1)]*4,[(0,1),(1,1),(1,0),(0,0)],[0,1,2,0,2,3],m)
    def node(name,prims):
        i=len(g['nodes']);g['nodes'].append(dict(name=name,mesh=len(g['meshes'])));g['meshes'].append(dict(name=name,primitives=prims));g['scenes'][0]['nodes'].append(i);return i
    node('Backdrop',[quad(-16,-7,-.45,32,16,paper)])
    node('Lettering',[quad(-3.4,-1.7,.05,17.4,4.35,lettering)])
    # Low polygon closed medallion: white bevel, cylindrical edge, back and logo face.
    N=48;cx=-8.2;cy=1.;ang=0.0
    def transform(p):
        x,y,z=p;return (x,y,z)
    def norm(p):
        x,y,z=p;return (x*math.cos(ang)+z*math.sin(ang),y,-x*math.sin(ang)+z*math.cos(ang))
    def disc(r,z,m,back=False):
        ps=[(0,0,z)]+[(r*math.cos(i*math.tau/N),r*math.sin(i*math.tau/N),z) for i in range(N)]
        uv=[(.5-x/(r*2),.5-y/(r*2)) for x,y,z in ps]
        # Texture x points to the right; V is top-down.
        uv=[(1-u,v) for u,v in uv]
        ids=[]
        for i in range(N):ids.extend([0,(i+1)%N+1,i+1] if back else [0,i+1,(i+1)%N+1])
        return primitive([transform(p) for p in ps],[norm((0,0,-1 if back else 1))]*len(ps),uv if m==face else None,ids,m,local=True)
    def ring(r0,z0,r1,z1,m):
        ps=[];ns=[];idx=[]
        for i in range(N):
            t=i*math.tau/N;c=math.cos(t);s=math.sin(t)
            ps.extend([(r0*c,r0*s,z0),(r1*c,r1*s,z1)])
            # Outward normal for the ring profile.
            nr=z1-z0;nz=r0-r1;l=math.hypot(nr,nz)
            ns.extend([norm((nr*c/l,nr*s/l,nz/l))]*2)
        for i in range(N):a0=i*2;b=((i+1)%N)*2;idx.extend([a0,b,a0+1,b,b+1,a0+1])
        return primitive([transform(p) for p in ps],ns,None,idx,m,local=True)
    coin=node('Medallion',[disc(3.25,.43,face),ring(3.65,.26,3.25,.43,rim),ring(3.65,-.26,3.65,.26,edge),disc(3.65,-.26,edge,True)])
    # HOME turned the medallion about its bone's origin even with the position on that
    # bone (it orbited the whole logo). The animated bone now has no translation at all:
    # a static parent places it, so the turn has only one possible centre.
    anchor=len(g['nodes'])
    g['nodes'].append(dict(name='MedallionPivot',translation=[-8.61,1,.735],children=[coin]))
    g['scenes'][0]['nodes'].remove(coin)
    g['scenes'][0]['nodes'].append(anchor)
    # Rotate the rigid coin about its own center, without vertical bob or scale squash.
    times=[i/60 for i in range(313)];rotations=[]
    for t in times:
        phase=(t/5.2)%1
        x=phase*2 if phase<.5 else (phase-.5)*2
        lo,hi=0.,1.
        for _ in range(32):
            u=(lo+hi)/2; bx=3*(1-u)**2*u*.42+3*(1-u)*u*u*.58+u**3
            if bx<x: lo=u
            else: hi=u
        u=(lo+hi)/2; eased=3*(1-u)*u*u+u**3
        pose=eased if phase<.5 else 1-eased
        yaw=math.radians(-24+48*pose)
        rotations.append((0,math.sin(yaw/2),0,math.cos(yaw/2)))
    g['animations']=[dict(name='COMMON',samplers=[dict(input=access(times,'SCALAR'),output=access(rotations,'VEC4'),interpolation='LINEAR')],channels=[dict(sampler=0,target=dict(node=coin,path='rotation'))])]
    g['cameras']=[dict(type='perspective',perspective=dict(aspectRatio=400/240,yfov=math.pi/6,znear=26.5,zfar=1000))]
    g['nodes'].append(dict(name='BannerCamera',camera=0,translation=[0,1,44.786]));g['scenes'][0]['nodes'].append(len(g['nodes'])-1)
    g['buffers']=[dict(uri='banner.bin',byteLength=len(buf))];(out/'banner.bin').write_bytes(buf);(out/'banner.gltf').write_text(json.dumps(g))
    sys.path.insert(0,str(a.pycgfx.resolve()));spec=importlib.util.spec_from_file_location('converter',a.pycgfx/'main.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
    import gltflib
    from cgfx.sobj import BillboardMode
    from cgfx.mtob import ColorFloat,FragmentLightingFlags,MTOBFlag,ConstantColorSource
    native=c.convert_gltf(gltflib.GLTF.load(str(out/'banner.gltf'),load_file_resources=True));model=native.data.models['COMMON']
    model.skeleton.bones.dict.regenerate()
    model.skeleton.bones['MedallionPivot'].billboard_mode=BillboardMode.Off
    for mesh in model.meshes.data.contents:
        shape=model.shapes.data.contents[mesh.shape_index];sets=shape.primitive_sets.data.contents
        assert len(sets)==1 and sets[0].skinning_mode==0
        bone=model.skeleton.bones[sets[0].related_bones.data.contents[0]];bone.billboard_mode=BillboardMode.Off if bone.name=='Medallion' else BillboardMode.YAxial
        mesh.mesh_node_name=bone.name;mesh.name=shape.name=f'{bone.name}_{mesh.shape_index}'
    for name in model.materials:
        m=model.materials[name];d=m.material_color.diffuse
        m.material_color.constant[0]=ColorFloat(d.r,d.g,d.b,d.a);m.flags=MTOBFlag(0)
        m.fragment_shader.fragment_lighting.flags=FragmentLightingFlags(0);m.fragment_shader.fragment_lighting_table.distribution_0_sampler=None
        for i in range(3):
            s=m.fragment_shader.texture_combiners[i];s.constant=ConstantColorSource.Constant0;s.src_rgb=s.src_alpha=0xFFF;s.combine_rgb=s.combine_alpha=0
        s=m.fragment_shader.texture_combiners[0];s.src_rgb=s.src_alpha=0x0E3 if m.used_texture_coordinates_count else 0xEEE;s.combine_rgb=s.combine_alpha=1 if m.used_texture_coordinates_count else 0
    # HOME cannot use RGBA8. LA8 gives the neutral oval 256 alpha levels at
    # two bytes/texel; RGBA4 would leave only 16 levels and visible banding.
    from cgfx.txob import TextureFormat
    from cgfx.swizzler import swizzle
    oval=native.data.textures['oval.png']; oval.hw_format=TextureFormat.LA8
    im=bg.transpose(Image.Transpose.FLIP_TOP_BOTTOM); raw=bytearray()
    for ty in range(im.height//8):
        for tx in range(im.width//8):
            tile=bytearray(128)
            for y in range(8):
                for x in range(8):
                    z=sum(((x>>b)&1)<<(2*b) | ((y>>b)&1)<<(2*b+1) for b in range(3))
                    r,gg,b,aa=im.getpixel((tx*8+x,ty*8+y));tile[2*z:2*z+2]=bytes((aa,r))
            raw.extend(tile)
    oval.pixel_based_image.data=bytes(raw)
    logo=native.data.textures['logo.png'];logo.hw_format=TextureFormat.RGB565
    logo.pixel_based_image.data=swizzle(Image.open(out/'logo.png').convert('RGBA').transpose(Image.Transpose.FLIP_TOP_BOTTOM),TextureFormat.RGB565)
    wordmark=native.data.textures['wordmark.png']
    wordmark.width=wordmark.pixel_based_image.width=512
    wordmark.height=wordmark.pixel_based_image.height=128
    wordmark.hw_format=TextureFormat.RGBA4
    wordmark.pixel_based_image.data=swizzle(text.transpose(Image.Transpose.FLIP_TOP_BOTTOM),TextureFormat.RGBA4)
    texture_bytes=sum(len(native.data.textures[name].pixel_based_image.data) for name in native.data.textures)
    data=c.write(native);(out/'banner.cgfx').write_bytes(data); report=inspect(data)
    triangles=sum(g['accessors'][p['indices']]['count']//3 for m in g['meshes'] for p in m['primitives'])
    assert triangles<=512 and len(data)<=384*1024
    subprocess.run(['ffmpeg','-v','error','-y','-i',str(ROOT/'assets/audio/intro-welcome.mp3'),'-af','apad,atrim=duration=3,afade=t=out:st=2.5:d=0.5','-ac','2','-ar','32000','-c:a','pcm_s16le',str(out/'banner.wav')],check=True)
    subprocess.run([str(Path.home()/'devkitpro/tools/bin/bannertool'),'makebanner','-ci',str(out/'banner.cgfx'),'-a',str(out/'banner.wav'),'-o',str(out/'banner.bnr')],check=True,stdout=subprocess.DEVNULL)
    unpacked,audio=unpack_banner((out/'banner.bnr').read_bytes());assert unpacked==data
    report.update(triangles=triangles,draw_calls=len(model.meshes.data.contents),texture_bytes=texture_bytes,pycgfx_commit=revision,loop_seconds=5.2,cbmd_roundtrip=True,audio_seconds=3,audio_channels=2,audio_rate=32000,home_menu_tested=False)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
