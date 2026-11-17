#!/usr/bin/env python3
"""Fresh small rigid HOME scene, static and front-facing by default.
No previous Forge scene or generator is imported. The optional reference supplies historical motion
only; all geometry, brand art and layout are authored here from shared assets.
"""
import argparse,hashlib,importlib.util,json,math,struct,subprocess,sys
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from fontTools.ttLib import TTFont
ROOT=Path(__file__).resolve().parents[2]
PIN='6c2e0cd53a5f4c3265b9aa9a1f30806f8291116e'

def reference_motion(data):
    def ptr(p):
        q=p+struct.unpack_from('<i',data,p)[0];assert 0<=q<len(data);return q
    def name(p):
        q=ptr(p);return data[q:data.index(b'\0',q)].decode()
    p=data.find(b'CANM');assert p>=0 and name(p+12)=='SkeletalAnimation'
    frames=struct.unpack_from('<f',data,p+20)[0];table=ptr(p+28);assert struct.unpack_from('<I',data,table+8)[0]==1
    member=ptr(table+40);assert name(member+4)=='ArwingFlight'
    tracks=[];times=None
    for field in (member+48,member+52,member+56):
        curve=ptr(field);assert struct.unpack_from('<I',data,curve+16)[0]==1
        segment=ptr(curve+20);start,end,fmt,count=struct.unpack_from('<2fII',data,segment)
        assert start==0 and end==frames and fmt>>5==6 and count==121
        pairs=[struct.unpack_from('<2f',data,segment+20+8*i) for i in range(count)]
        ts=[t for t,v in pairs];vs=[v for t,v in pairs]
        assert all(math.isfinite(v) and abs(v)<.25 for v in vs) and abs(vs[0]-vs[-1])<1e-6
        assert times is None or ts==times;times=ts;tracks.append(vs)
    return [t/60 for t in times],list(zip(*tracks)),frames

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--pycgfx',type=Path,required=True);ap.add_argument('--starfox-model',type=Path);motion=ap.add_mutually_exclusive_group();motion.add_argument('--animate-starfox',action='store_true',help='Reproduce rejected historical test 18');motion.add_argument('--minish-turn',action='store_true',help='One central rigid mesh and the continuous native Minish Cap transform');motion.add_argument('--starfox-motion',action='store_true',help='Translation only, with absent scale/rotation channels as in Starfox');ap.add_argument('--background-depth',type=float,default=-.47);ap.add_argument('--background-scale',type=float,default=1.);ap.add_argument('--top-fade-inset',type=float,default=0.,help='Inset only the upper half of the oval, in texture pixels');ap.add_argument('--out',type=Path,default=ROOT/'3ds/build/home-banner/static-19');a=ap.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    assert -16. <= a.background_depth <= -.47, 'background depth outside reviewed range'
    assert 1. <= a.background_scale <= 2., 'background scale outside reviewed range'
    assert 0. <= a.top_fade_inset <= 24., 'top fade inset outside reviewed range'
    animated=a.animate_starfox or a.starfox_motion
    assert subprocess.check_output(['git','-C',str(a.pycgfx),'rev-parse','HEAD'],text=True).strip()==PIN
    reference=b'';times=[];offsets=[];frames=0
    if animated:
        assert a.starfox_model, 'animation requires --starfox-model'
        reference=a.starfox_model.read_bytes();times,offsets,frames=reference_motion(reference)
    for f,n in [('468d56b6b25b05b70190.woff2','brand'),('20fc9b6fc618e7c3c68d.woff2','ui')]:
        t=TTFont(ROOT/'assets/fonts'/f);t.flavor=None;t.save(out/(n+'.ttf'))
    paper=Image.new('RGBA',(256,128));px=paper.load()
    for y in range(128):
        for x in range(256):
            ry=62-a.top_fade_inset if y<63.5 else 62
            r=math.hypot((x-127.5)/126,(y-63.5)/ry);v=max(0,min(1,(1-r)/.65));l=242 if not (x%7==3 and y%7==3) else 204;px[x,y]=(l,l,l,round(255*v*v*(3-2*v)))
    paper.save(out/'paper.png')
    title=Image.new('RGBA',(512,128));d=ImageDraw.Draw(title)
    bold=ImageFont.truetype(str(out/'brand.ttf'),88);bold.set_variation_by_axes([800]);regular=ImageFont.truetype(str(out/'brand.ttf'),88);regular.set_variation_by_axes([400]);ui=ImageFont.truetype(str(out/'ui.ttf'),28);ui.set_variation_by_axes([800])
    d.text((0,66),'forge.',font=bold,fill='#dd741b',anchor='ls');d.text((round(d.textlength('forge.',font=bold)),66),'store',font=regular,fill='#2c3e45',anchor='ls');d.text((4,119),'Ports for Nintendo 3DS',font=ui,fill='#465960',anchor='ls');
    if a.minish_turn:
        centered=Image.new('RGBA',title.size)
        for top,bottom in ((0,86),(86,128)):
            line=title.crop((0,top,512,bottom));bounds=line.getbbox();assert bounds
            ink=line.crop((bounds[0],0,bounds[2],bottom-top))
            centered.alpha_composite(ink,((512-ink.width)//2,top))
        title=centered
    title=title.point(lambda n:n//16*17);title.save(out/'title.png')
    Image.open(ROOT/'assets/brand/forge-icon.png').convert('RGBA').resize((128,128),Image.Resampling.NEAREST).save(out/'face.png')
    doc=dict(asset={'version':'2.0','generator':'Forge fresh rigid banner'},scene=0,scenes=[{'nodes':[0,1,2]}],nodes=[],meshes=[],materials=[],images=[],textures=[],samplers=[dict(magFilter=9729,minFilter=9729,wrapS=33071,wrapT=33071)],accessors=[],bufferViews=[],buffers=[],animations=[]);buf=bytearray()
    def acc(rows,kind,ctype=5126):
        while len(buf)%4:buf.append(0)
        start=len(buf);n={'SCALAR':1,'VEC2':2,'VEC3':3}[kind]
        for row in rows:buf.extend(struct.pack('<'+('f' if ctype==5126 else 'H')*n,*(row if isinstance(row,(tuple,list)) else [row])))
        doc['bufferViews'].append(dict(buffer=0,byteOffset=start,byteLength=len(buf)-start));d=dict(bufferView=len(doc['bufferViews'])-1,componentType=ctype,count=len(rows),type=kind)
        if kind=='VEC3':d.update(min=[min(r[i] for r in rows) for i in range(3)],max=[max(r[i] for r in rows) for i in range(3)])
        doc['accessors'].append(d);return len(doc['accessors'])-1
    def material(name,color,image=None,alpha=False):
        p=dict(baseColorFactor=color,metallicFactor=0,roughnessFactor=1)
        if image:doc['images'].append(dict(uri=image));doc['textures'].append(dict(source=len(doc['images'])-1,sampler=0));p['baseColorTexture']=dict(index=len(doc['textures'])-1)
        doc['materials'].append(dict(name=name,pbrMetallicRoughness=p,alphaMode='BLEND' if alpha else 'OPAQUE',doubleSided=False));return len(doc['materials'])-1
    bg=material('Paper',[1,1,1,1],'paper.png',True);text=material('Title',[1,1,1,1],'title.png',True);front=material('Face',[1,1,1,1],'face.png');white=material('Rim',[1,1,1,1]);grey=material('Edge',[.68,.73,.76,1])
    def primitive(vertices,normals,uv,indices,mat):
        attrs=dict(POSITION=acc(vertices,'VEC3'),NORMAL=acc(normals,'VEC3'))
        if uv:attrs['TEXCOORD_0']=acc(uv,'VEC2')
        return dict(attributes=attrs,indices=acc(indices,'SCALAR',5123),material=mat)
    def plane(x,y,z,w,h,mat):return primitive([(x,y,z),(x+w,y,z),(x+w,y+h,z),(x,y+h,z)],[(0,0,1)]*4,[(0,1),(1,1),(1,0),(0,0)],[0,1,2,0,2,3],mat)
    def node(name,parts):doc['nodes'].append(dict(name=name,mesh=len(doc['meshes'])));doc['meshes'].append(dict(name=name,primitives=parts))
    k=a.background_scale
    backdrop=plane(-16.8*k,1.-8.4*k,a.background_depth,33.6*k,16.8*k,bg);wordmark=plane(-9.135 if a.minish_turn else -3.57,-5.568 if a.minish_turn else -1.835,.05,18.27,4.568,text)
    node('ForgeBackdrop',[backdrop]);node('ForgeWordmark',[wordmark])
    n=40;angle=math.radians(-18) if a.animate_starfox else 0.;c=math.cos(angle);s=math.sin(angle)
    def pos(v):
        x,y,z=v
        return (x,y,z) if a.minish_turn else (-8.61+c*x+s*z,1+y,.735-s*x+c*z)
    def normal(v):x,y,z=v;return (c*x+s*z,y,-s*x+c*z)
    def disc(r,z,mat,back=False):
        ps=[(0,0,z)]+[(r*math.cos(i*math.tau/n),r*math.sin(i*math.tau/n),z) for i in range(n)];idx=[]
        for i in range(n):idx.extend([0,(i+1)%n+1,i+1] if back else [0,i+1,(i+1)%n+1])
        return primitive([pos(v) for v in ps],[normal((0,0,-1 if back else 1))]*len(ps),[(.5+(-x if back else x)/(2*r),.5-y/(2*r)) for x,y,z in ps] if mat==front else None,idx,mat)
    def ring(r0,z0,r1,z1,mat):
        ps=[];ns=[];idx=[];nr=z1-z0;nz=r0-r1;length=math.hypot(nr,nz)
        for i in range(n):
            co=math.cos(i*math.tau/n);si=math.sin(i*math.tau/n);ps.extend([(r0*co,r0*si,z0),(r1*co,r1*si,z1)]);ns.extend([normal((nr*co/length,nr*si/length,nz/length))]*2)
        for i in range(n):a0=i*2;b0=(i+1)%n*2;idx.extend([a0,b0,a0+1,b0,b0+1,a0+1])
        return primitive([pos(v) for v in ps],ns,None,idx,mat)
    parts=[disc(3.41,.45,front),ring(3.83,.27,3.41,.45,white),ring(3.83,-.27,3.83,.27,grey)]
    if a.minish_turn:
        parts.extend([ring(3.41,-.45,3.83,-.27,white),disc(3.41,-.45,front,True)])
        node('ForgeTurnCoin',parts);doc['nodes'][-1]['translation']=[0.,3.8,0.]
    else:node('ForgeMedallion',parts+[disc(3.83,-.27,grey,True)])
    if animated:
        doc['animations']=[dict(name='COMMON',samplers=[dict(input=acc(times,'SCALAR'),output=acc(offsets,'VEC3'),interpolation='LINEAR')],channels=[dict(sampler=0,target=dict(node=2,path='translation'))])]
    if a.minish_turn:
        # Match Minish's opaque model first, then independent transparent art.
        # The converter sorts the bone table but does not repair sibling links.
        backdrop_node,wordmark_node,coin_node=doc['nodes']
        doc['nodes']=[dict(name='COMMON',children=[1,2,3]),coin_node,backdrop_node,wordmark_node];doc['scenes'][0]['nodes']=[0]
    doc['buffers']=[dict(uri='scene.bin',byteLength=len(buf))];(out/'scene.bin').write_bytes(buf);(out/'scene.gltf').write_text(json.dumps(doc))
    sys.path.insert(0,str(a.pycgfx.resolve()));spec=importlib.util.spec_from_file_location('converter',a.pycgfx/'main.py');cv=importlib.util.module_from_spec(spec);spec.loader.exec_module(cv)
    import gltflib
    from cgfx.sobj import BillboardMode
    from cgfx.mtob import ColorFloat,FragmentLightingFlags,MTOBFlag,ConstantColorSource
    from cgfx.cgfx import CGFXData
    from cgfx.dict import DictInfo
    from cgfx.txob import TextureFormat
    from cgfx.swizzler import swizzle
    native=cv.convert_gltf(gltflib.GLTF.load(str(out/'scene.gltf'),load_file_resources=True));model=native.data.models['COMMON']
    if a.minish_turn:
        from cgfx.canm import CANM,CANMBoneTransform,FloatAnimationCurve,FloatSegment,StepLinear64Key,QuantizationType
        animation=CANM();animation.name='COMMON';animation.target_animation_group_name='SkeletalAnimation';animation.frame_size=270.
        member=CANMBoneTransform();member.bone_path='ForgeTurnCoin'
        for channel,value in [('scale_x',1.),('scale_y',1.),('scale_z',1.),('pos_x',0.),('pos_y',3.8),('pos_z',0.),('rot_y',None)]:
            curve=FloatAnimationCurve();curve.end_frame=270.;segment=FloatSegment();segment.end_frame=270.;segment.quantization=QuantizationType.StepLinear64
            segment.keys=[StepLinear64Key(0.,0. if value is None else value),StepLinear64Key(270.,math.tau if value is None else value)];curve.segments=[segment];setattr(member,channel,curve)
        member.rot_x=member.rot_z=0.;animation.member_animations_data.add(member.bone_path,member);native.data.skeletal_animations.add(animation.name,animation)
    model.skeleton.bones.dict.regenerate()
    for mesh in model.meshes.data.contents:
        shape=model.shapes.data.contents[mesh.shape_index];sets=shape.primitive_sets.data.contents;assert len(sets)==1 and sets[0].skinning_mode==0;bone=model.skeleton.bones[sets[0].related_bones.data.contents[0]];bone.billboard_mode=BillboardMode.Off if a.minish_turn and bone.name=='ForgeTurnCoin' else BillboardMode.YAxial;mesh.mesh_node_name=bone.name;mesh.name=shape.name=f'{bone.name}_{mesh.shape_index}'
    for name in model.materials:
        m=model.materials[name]
        d=m.material_color.diffuse;m.material_color.constant[0]=ColorFloat(d.r,d.g,d.b,d.a);m.flags=MTOBFlag(0);m.fragment_shader.fragment_lighting.flags=FragmentLightingFlags(0);m.fragment_shader.fragment_lighting_table.distribution_0_sampler=None
        for i in range(3):
            t=m.fragment_shader.texture_combiners[i];t.constant=ConstantColorSource.Constant0;t.src_rgb=t.src_alpha=0xFFF;t.combine_rgb=t.combine_alpha=0
        t=m.fragment_shader.texture_combiners[0];t.src_rgb=t.src_alpha=0x0E3 if m.used_texture_coordinates_count else 0xEEE;t.combine_rgb=t.combine_alpha=1 if m.used_texture_coordinates_count else 0
    for name in native.data.textures:
        tx=native.data.textures[name];im=Image.open(out/name).convert('RGBA').transpose(Image.Transpose.FLIP_TOP_BOTTOM);tx.width=tx.pixel_based_image.width=im.width;tx.height=tx.pixel_based_image.height=im.height
        if name=='paper.png':
            tx.hw_format=TextureFormat.LA8;raw=bytearray()
            for ty in range(im.height//8):
                for xx in range(im.width//8):
                    tile=bytearray(128)
                    for y in range(8):
                        for x in range(8):
                            z=sum((((x>>b)&1)<<(2*b))|(((y>>b)&1)<<(2*b+1)) for b in range(3));r,gg,b,aa=im.getpixel((xx*8+x,ty*8+y));tile[2*z:2*z+2]=bytes((aa,r))
                    raw.extend(tile)
            tx.pixel_based_image.data=bytes(raw)
        else:tx.hw_format=TextureFormat.RGB565 if name=='face.png' else TextureFormat.RGBA4;tx.pixel_based_image.data=swizzle(im,tx.hw_format)
    class FullData(CGFXData):
        struct=struct.Struct('4si'+DictInfo.struct.format*16)
        def values(self):
            v=super().values();return (*v[:-1],self.fog_animations,v[-1])
    native.data.__class__=FullData;native.data.fog_animations=DictInfo();native.struct=struct.Struct(native.header.struct.format+FullData.struct.format)
    if a.animate_starfox:
        member=native.data.skeletal_animations['COMMON'].member_animations_data['ForgeMedallion'];member.scale_x=member.scale_y=member.scale_z=1.;member.rot_x=member.rot_y=member.rot_z=0.
    elif a.starfox_motion:
        member=native.data.skeletal_animations['COMMON'].member_animations_data['ForgeMedallion']
        # Starfox's actual CANM has no scale/rotation channels. Do not turn
        # absent channels into constant keys: HOME's animation path differs.
        assert all(getattr(member,n) is None for n in ('scale_x','scale_y','scale_z','rot_x','rot_y','rot_z'))
    data=bytearray(cv.write(native))
    if animated or a.minish_turn:
        flags=struct.unpack_from('<I',data,member.offset)[0];struct.pack_into('<I',data,member.offset,flags|0x400000)
    if a.starfox_motion:
        assert flags==0x3f0000, 'reference sparse transform encoding changed'
        assert data[member.offset+20:member.offset+48]==bytes(28), 'absent channel payload'
    data=bytes(data);assert len(data)<0x80000;(out/'banner.cgfx').write_bytes(data)
    subprocess.run(['ffmpeg','-v','error','-y','-i',str(ROOT/'assets/audio/intro-welcome.mp3'),'-af','apad,atrim=duration=3,afade=t=out:st=2.5:d=0.5','-ac','2','-ar','32000','-c:a','pcm_s16le',str(out/'banner.wav')],check=True)
    report=dict(kind='single central rigid 3D Minish-style continuous turn' if a.minish_turn else 'sparse Starfox translation' if a.starfox_motion else 'historical Starfox motion' if a.animate_starfox else 'static front-facing banner',background_scale=a.background_scale,background_depth=a.background_depth,top_fade_inset=a.top_fade_inset,background_dimensions=[33.6*k,16.8*k],reference_sha256=hashlib.sha256(reference).hexdigest() if reference else None,reference_frames=frames,reference_keys=len(times),reference_translation_offsets=offsets,bytes=len(data),meshes=7 if a.minish_turn else 6,triangles=324 if a.minish_turn else 244,texture_bytes=229376,rotating_bones=1 if a.minish_turn else 0,material_animation_members=0,hardware_tested=False)
    (out/'report.json').write_text(json.dumps(report,indent=2));print({k:v for k,v in report.items() if k!='reference_translation_offsets'})
if __name__=='__main__':main()
