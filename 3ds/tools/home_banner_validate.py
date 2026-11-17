#!/usr/bin/env python3
"""Inspect serialized HOME resources. These checks do not certify hardware.

Patricia traversal follows Star Fox's validate-cgfx-dicts.py. The rendered-bone
and mesh-link checks follow its sculpted-v2/tools/check_fixed_parts.py.
"""
import argparse
import hashlib
import math
from pathlib import Path
import struct

if not __debug__:
    raise RuntimeError("Run the banner validator without Python optimization (-O).")


def dictionaries(data):
    data=bytes(data)
    assert data[:4] == b'CGFX' and data[4:6] == b'\xff\xfe', 'CGFX header'
    assert len(data) == struct.unpack_from('<I', data, 12)[0] <= 0x80000, 'CGFX size'
    result = []
    for off in range(0, len(data)-12, 4):
        if data[off:off+4] != b'DICT':
            continue
        size, count = struct.unpack_from('<II', data, off+4)
        if size != 12 + 16*(count+1):
            continue
        assert off+size <= len(data), 'DICT bounds'
        rows = []
        for i in range(count+1):
            p = off+12+16*i
            bit, left, right, namep, objp = struct.unpack_from('<IHHii', data, p)
            assert left <= count and right <= count, 'DICT child'
            def string(at, relative):
                if not relative:
                    return b''
                address = at+relative
                assert 0 <= address < len(data), 'string pointer'
                return data[address:data.index(b'\0', address)]
            name = string(p+8, namep)
            target = p+12+objp if objp else 0
            assert not objp or 0 <= target < len(data), 'object pointer'
            rows.append((bit, left, right, name, target))
        assert rows[0][0] == 0xffffffff, 'DICT root'
        assert len({r[3] for r in rows[1:]}) == count, 'DICT duplicate key'
        for expected in range(1, count+1):
            key = rows[expected][3]
            assert key, 'DICT empty key'
            parent, current = 0, rows[0][1]
            for _ in range(count+2):
                if rows[parent][0] <= rows[current][0]:
                    break
                parent = current
                bit = rows[current][0]
                byte = key[bit//8] if bit//8 < len(key) else 0
                current = rows[current][2 if byte >> (bit%8) & 1 else 1]
            else:
                raise AssertionError('DICT lookup cycle')
            assert current == expected, ('DICT unresolved key', key)
        result.append(rows)
    assert result, 'no dictionaries'
    return result


def animations(data):
    def u32(p): return struct.unpack_from('<I', data, p)[0]
    def pointer(p):
        relative = struct.unpack_from('<i', data, p)[0]
        assert relative != 0, 'null animation curve/reference'
        q = p + relative
        assert 0 <= q < len(data), 'animation pointer'
        return q
    def string(p):
        q = pointer(p)
        return data[q:data.index(b'\0', q)].decode()
    result = []
    for p in range(0, len(data)-40, 4):
        if data[p:p+4] != b'CANM' or string(p+12) != 'SkeletalAnimation':
            continue
        duration = struct.unpack_from('<f', data, p+20)[0]
        assert math.isfinite(duration) and 0 < duration <= 3600 and u32(p+16) == 1, 'animation duration/loop'
        count, table = u32(p+24), pointer(p+28)
        assert data[table:table+4] == b'DICT' and u32(table+8) == count
        curve_count = 0
        rotation_tracks = 0
        members = []
        for i in range(1, count+1):
            bone = pointer(table+12+16*i+12)
            assert u32(bone+16) == 5, 'skeletal member is not a rigid Transform'
            flags = u32(bone)
            # The skipped field at +44 is Rotation W, not anonymous padding.
            # Euler Transform (5) must mark that tenth slot absent (bit 22).
            assert flags & 0x00400000, 'Rotation W is present but has no curve'
            assert not flags & 0x1000 and u32(bone+44)==0, 'unexpected Rotation W payload'
            members.append(string(bone+4))
            const = [0x40,0x80,0x100,0x200,0x400,0x800,0x2000,0x4000,0x8000]
            ignored = [0x10000,0x20000,0x40000,0x80000,0x100000,0x200000,0x800000,0x1000000,0x2000000]
            tracks = {}
            for slot in range(9):
                field = bone+20+4*slot+(4 if slot >= 6 else 0)
                if flags & ignored[slot]:
                    assert u32(field) == 0
                    continue
                values = []
                if flags & const[slot]:
                    values = [struct.unpack_from('<f', data, field)[0]]
                else:
                    if 3 <= slot <= 5: rotation_tracks += 1
                    curve = pointer(field)
                    first, last = struct.unpack_from('<2f', data, curve)
                    assert math.isfinite(first) and math.isfinite(last) and 0 <= first <= last <= duration
                    segments = u32(curve+16)
                    assert 0 < segments <= 1000
                    for j in range(segments):
                        segment = pointer(curve+20+4*j)
                        a, b, f = struct.unpack_from('<2fI', data, segment)
                        assert math.isfinite(a) and math.isfinite(b) and 0 <= a < b <= duration
                        if f & 1:
                            values.append(struct.unpack_from('<f', data, segment+12)[0])
                        else:
                            assert f >> 5 == 6, 'unexpected curve quantization'
                            n = u32(segment+12)
                            assert 1 <= n <= 65536 and segment+20+8*n <= len(data)
                            keys = [struct.unpack_from('<2f', data, segment+20+8*k) for k in range(n)]
                            assert all(math.isfinite(t) and a <= t <= b for t, v in keys)
                            assert all(keys[k][0] <= keys[k+1][0] for k in range(n-1))
                            values.extend(v for t, v in keys)
                    curve_count += 1
                tracks[slot] = values
                assert all(math.isfinite(v) for v in values), 'non-finite animation value'
                assert values and abs(values[0]-values[-1]) < 1e-6, 'animation loop is not closed'
                if slot <= 2:
                    assert all(v > 0 for v in values), 'singular scale in animation'
                if 3 <= slot <= 5:
                    assert members[-1] == 'Medallion' and all(abs(v) <= .5 for v in values), 'unexpected rotation'
                    if slot != 4: assert all(v == 0 for v in values), 'rotation must be lateral'
                if slot == 7: assert all(v == 1 for v in values), 'vertical bob forbidden'
            if members[-1] == 'Medallion':
                yaw = tracks[4]
                assert min(yaw) < -.4 and max(yaw) > .4, 'missing lateral swing'
                assert set(tracks) == set(range(9)), 'all rigid TRS components must be explicit'
                for slot, expected in ((6,-8.61),(7,1.),(8,.735)):
                    assert all(abs(v-expected)<1e-5 for v in tracks[slot]), 'moving medallion pivot'
        result.append(dict(frames=duration, members=members, curves=curve_count, scales_positive=True, rotation_tracks=rotation_tracks,rotation_w_absent=True))
    return result


def resource_table(data):
    """DATA has 16 count/pointer pairs, including FogAnimations and Emitters."""
    assert len(data)>=156 and data[20:24]==b'DATA', 'DATA resource table'
    end=20+struct.unpack_from('<I',data,24)[0]
    assert 156<=end<=len(data), 'DATA resource table bounds'
    result=[]
    for i in range(16):
        field=28+i*8
        count,relative=struct.unpack_from('<Ii',data,field)
        if count==0:
            assert relative==0, 'empty DATA resource has a pointer'
        else:
            target=field+4+relative
            assert relative and 156<=target<=end-12, 'missing/overlapping DATA resource slot'
            assert data[target:target+4]==b'DICT', 'DATA resource does not target a dictionary'
            size,actual=struct.unpack_from('<II',data,target+4)
            assert count==actual and size==12+16*(count+1), 'DATA resource count mismatch'
            assert target+size<=end, 'DATA resource exceeds block'
        result.append(count)
    assert result[0]==1 and result[1]==3 and ((result[9],result[10]) in ((0,0),(1,0),(0,1))), 'unexpected model/texture/animation resources'
    assert result[14:]==[0,0], 'unexpected fog animation or emitter'
    return result


def require_static(data):
    """Production recovery policy after animated test 20 froze physical HOME."""
    assert not any(resource_table(data)[9:]), 'Animated HOME banners are rejected for the recovery build'
    return True


def pose_animations(data):
    def ptr(p):
        v=struct.unpack_from('<i',data,p)[0];q=p+v
        assert v and 0<=q<len(data), 'pose animation pointer'
        return q
    tracks={}
    for p in range(0,len(data)-40,4):
        if data[p:p+4]!=b'CANM':continue
        group=ptr(p+12)
        assert data[group:data.index(b'\0',group)]==b'MaterialAnimation', 'pose animation target'
        duration=struct.unpack_from('<f',data,p+20)[0];assert duration==312, 'pose duration'
        table=ptr(p+28);count=struct.unpack_from('<I',data,table+8)[0];assert count==27, 'pose member count'
        for i in range(1,count+1):
            m=ptr(table+12+16*i+12);flags=struct.unpack_from('<I',data,m)[0];n=ptr(m+4);name=data[n:data.index(b'\0',n)].decode()
            assert name.startswith('Materials["Pose') and name.endswith('.MaterialColor.Constant0'), 'pose material binding'
            assert struct.unpack_from('<I',data,m+16)[0]==6 and flags==7, 'RGBA pose flags'
            curve=ptr(m+32);segment=ptr(curve+20)
            assert struct.unpack_from('<I',data,curve+16)[0]==1, 'pose curve segment'
            first,last,fmt,count=struct.unpack_from('<2fII',data,segment)
            assert (first,last,fmt>>5,count)==(0.,312.,6,121), 'pose curve layout'
            keys=[struct.unpack_from('<2f',data,segment+20+8*j) for j in range(count)]
            assert all(math.isfinite(t) and math.isfinite(v) and 0<=v<=1 for t,v in keys), 'pose opacity bounds'
            assert all(keys[j][0]<keys[j+1][0] for j in range(count-1)), 'pose key order'
            assert abs(keys[0][1]-keys[-1][1])<1e-6, 'pose loop'
            tracks[name]=[v for t,v in keys]
    assert len(tracks)==27, 'missing pose animation'
    for k in range(121):
        weights=[tracks[f'Materials["Pose{p:02d}_0"].MaterialColor.Constant0'][k] for p in range(9)]
        assert abs(math.prod(1-v for v in weights))<1e-6 and sum(v>1e-6 for v in weights)<=2, 'pose blend weights'
        for p in range(9):
            assert all(abs(tracks[f'Materials["Pose{p:02d}_{part}"].MaterialColor.Constant0'][k]-weights[p])<1e-6 for part in (1,2)), 'pose parts disagree'
    return dict(poses=9,members=27,keys=121,loop_frames=312,source_over_coverage_opaque=True)


def inspect_fresh(data,bones,tables,resources):
    assert resources[9] in (0,1) and not any(resources[10:]), 'fresh banner has unexpected animation resources'
    required={'ForgeBackdrop','ForgeWordmark','ForgeMedallion'}
    assert required<=set(bones), 'missing fresh scene nodes'
    for n in required:
        b=bones[n]
        assert b['mode']==5 and b['trs']==(1.,1.,1.,0.,0.,0.,0.,0.,0.), 'fresh rigid node must have identity rest and facing mode'
    def ptr(p):
        v=struct.unpack_from('<i',data,p)[0];q=p+v;assert v and 0<=q<len(data), 'fresh animation pointer';return q
    def name(p):
        q=ptr(p);return data[q:data.index(b'\0',q)].decode()
    mesh_count=0
    for p in range(0,len(data)-116,4):
        if data[p:p+8]==b'\0\0\0\x01SOBJ':
            assert name(p+112) in required, 'unbound fresh mesh';mesh_count+=1
    assert mesh_count==6, 'fresh mesh count'
    if resources[9]==0:
        assert b'CANM' not in data, 'static banner contains animation'
        return dict(profile='fresh-static-front',bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),meshes=mesh_count,dictionaries=len(tables),resource_slots=16,rotation_tracks=0,translation_tracks=0,all_rendered_rest_transforms_identity=True,hardware_tested=False)
    p=data.find(b'CANM');assert p>=0 and name(p+12)=='SkeletalAnimation'
    assert struct.unpack_from('<f',data,p+20)[0]==240 and struct.unpack_from('<I',data,p+16)[0]==1, 'fresh animation loop'
    table=ptr(p+28);assert struct.unpack_from('<I',data,table+8)[0]==1
    m=ptr(table+40);flags=struct.unpack_from('<I',data,m)[0]
    assert name(m+4)=='ForgeMedallion' and struct.unpack_from('<I',data,m+16)[0]==5
    assert flags&0x400000 and struct.unpack_from('<I',data,m+44)[0]==0, 'Rotation W absence'
    sparse=flags==0x7f0000
    if sparse:
        assert data[m+20:m+48]==bytes(28), 'sparse animation contains scale/rotation payload'
    else:
        assert flags&0xfc0==0xfc0, 'scales/rotations must be constant'
        assert struct.unpack_from('<6f',data,m+20)==(1.,1.,1.,0.,0.,0.), 'fresh rotation or scale'
    tracks=[]
    for field in (m+48,m+52,m+56):
        curve=ptr(field);assert struct.unpack_from('<I',data,curve+16)[0]==1
        seg=ptr(curve+20);start,end,fmt,count=struct.unpack_from('<2fII',data,seg)
        assert (start,end,fmt>>5,count)==(0.,240.,6,121), 'fresh translation curve'
        keys=[struct.unpack_from('<2f',data,seg+20+8*i) for i in range(count)]
        assert all(math.isfinite(t) and math.isfinite(v) and abs(v)<.25 for t,v in keys)
        assert abs(keys[0][1]-keys[-1][1])<1e-6 and all(keys[i][0]<keys[i+1][0] for i in range(count-1)), 'fresh closed keys'
        tracks.append(keys)
    assert all([t for t,v in a]==[t for t,v in tracks[0]] for a in tracks)
    return dict(profile='fresh-starfox-sparse' if sparse else 'fresh-starfox-translation',bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),meshes=mesh_count,dictionaries=len(tables),resource_slots=16,rotation_tracks=0,translation_tracks=3,keys_per_track=121,loop_frames=240,all_rendered_rest_transforms_identity=True,hardware_tested=False)


def inspect(data):
    resources = resource_table(data)
    tables = dictionaries(data)
    def u32(p): return struct.unpack_from('<I', data, p)[0]
    def name(p):
        relative = struct.unpack_from('<i', data, p)[0]
        if not relative:
            return ''
        p += relative
        assert 0 <= p < len(data), 'name pointer'
        return data[p:data.index(b'\0', p)].decode('utf-8')
    bones = {}
    for rows in tables:
        # Identify the bone table by its Scene root entry, then inspect each
        # object's own name. Do not mistake CANM members for skeleton bones.
        if not any(r[3].startswith(b'Scene root') for r in rows):
            continue
        for _, _, _, key, p in rows[1:]:
            try:
                own_name = name(p)
            except (AssertionError, ValueError, UnicodeDecodeError):
                continue
            if own_name != key.decode():
                continue
            assert p+216 <= len(data), 'bone bounds'
            bones[key.decode()] = dict(offset=p, mode=u32(p+212), trs=struct.unpack_from('<9f', data, p+32), parent=name(p+16+struct.unpack_from('<i',data,p+16)[0]) if struct.unpack_from('<i',data,p+16)[0] else '')
    if 'ForgeTurnCoin' in bones:
        assert resources[9:]==[1]+[0]*6, 'continuous rigid turn resources'
        coin=bones['ForgeTurnCoin'];assert coin['parent']=='COMMON' and coin['mode']==0
        order=['Scene root','COMMON','ForgeTurnCoin','ForgeBackdrop','ForgeWordmark']
        assert list(bones)==order, 'opaque-first bone dictionary order'
        def linked(n,field):
            p=bones[n]['offset']+field;rel=struct.unpack_from('<i',data,p)[0]
            return name(p+rel) if rel else ''
        assert linked('COMMON',20)=='ForgeTurnCoin' and linked('ForgeTurnCoin',28)=='ForgeBackdrop' and linked('ForgeBackdrop',28)=='ForgeWordmark' and linked('ForgeWordmark',28)=='', 'opaque-first hierarchy links'
        assert all(abs(x-y)<1e-5 for x,y in zip(coin['trs'],(1.,1.,1.,0.,0.,0.,0.,3.8,0.))), 'central vertical axis'
        for n in ('ForgeBackdrop','ForgeWordmark'):
            b=bones[n];assert b['parent']=='COMMON' and b['mode']==5 and b['trs']==(1.,1.,1.,0.,0.,0.,0.,0.,0.)
        def ptr(p):
            q=p+struct.unpack_from('<i',data,p)[0];assert 0<=q<len(data);return q
        p=data.find(b'CANM');assert name(p+12)=='SkeletalAnimation' and u32(p+16)==1 and struct.unpack_from('<f',data,p+20)[0]==270.
        table=ptr(p+28);assert u32(table+8)==1;m=ptr(table+40)
        assert name(m+4)=='ForgeTurnCoin' and u32(m)==0x400a00 and u32(m+16)==5, 'Minish-style continuous transform'
        assert u32(m+32)==u32(m+40)==u32(m+44)==0, 'no X/Z/W rotation'
        for field,values in ((20,(1.,1.)),(24,(1.,1.)),(28,(1.,1.)),(36,(0.,math.tau)),(48,(0.,0.)),(52,(3.8,3.8)),(56,(0.,0.))):
            c=ptr(m+field);assert struct.unpack_from('<2f',data,c)==(0.,270.) and u32(c+16)==1
            seg=ptr(c+20);assert struct.unpack_from('<2fII',data,seg)==(0.,270.,196,2)
            t0,v0,t1,v1=struct.unpack_from('<4f',data,seg+20);assert (t0,t1)==(0.,270.) and abs(v0-values[0])<1e-6 and abs(v1-values[1])<1e-6
        return dict(profile='diagnostic-continuous-central-3d',bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),meshes=7,triangles=324,rotation_tracks=1,constant_pivot_translation_curves=3,scale_curves=3,loop_frames=270,centered_above_wordmark=True,hardware_tested=False)
    if 'ForgeMedallion' in bones:
        return inspect_fresh(data,bones,tables,resources)
    assert bones.get('Medallion',{}).get('parent') == 'Scene root', 'use a direct rigid mesh node'
    meshes = []
    for p in range(0, len(data)-116, 4):
        if data[p:p+8] != b'\0\0\0\x01SOBJ':
            continue
        target = name(p+112)
        assert target in bones, ('mesh has no resolved bone', target, hex(p))
        b = bones[target]
        assert b['mode'] == (0 if target == 'Medallion' and resources[9] else 5), ('unexpected billboard mode', target, b['mode'])
        expected=(1.,1.,1.,0.,0.,0.,-8.61,1.,.735) if target=='Medallion' and resources[9] else (1.,1.,1.,0.,0.,0.,0.,0.,0.)
        assert all(abs(a-b)<1e-5 for a,b in zip(b['trs'],expected)), ('unexpected rest transform',target,b['trs'])
        meshes.append(target)
    assert meshes, 'no meshes'
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                resource_slots=len(resources), dictionaries=len(tables), dictionary_keys=sum(len(t)-1 for t in tables),
                meshes=len(meshes), rendered_bones=sorted(set(meshes)),
                skeletal_animations=animations(data),
                material_poses=pose_animations(data) if resources[10] else None,
                all_mesh_links_resolve=True, medallion_billboard_off=bool(resources[9]),
                medallion_local_pivot=True, hardware_tested=False)


def unpack_banner(data):
    assert data[:4] == b'CBMD' and len(data) >= 0x88
    start, audio = struct.unpack_from('<I', data, 8)[0], struct.unpack_from('<I', data, 0x84)[0]
    assert 0x88 <= start < audio < len(data)
    assert data[audio:audio+4] == b'CWAV'
    assert struct.unpack_from('<I', data, audio+12)[0] == len(data)-audio, 'CWAV size'
    stream = data[start:audio]
    assert stream[0] == 0x11
    size = int.from_bytes(stream[1:4], 'little')
    assert 0 < size <= 0x80000
    pos, out = 4, bytearray()
    while len(out) < size:
        flags = stream[pos]; pos += 1
        for bit in range(7, -1, -1):
            if len(out) == size:
                break
            b = stream[pos]; pos += 1
            if not flags >> bit & 1:
                out.append(b); continue
            hi = b >> 4
            if hi == 0:
                c, d = stream[pos:pos+2]; pos += 2
                length, distance = ((b & 15) << 4 | c >> 4)+0x11, ((c & 15) << 8 | d)+1
            elif hi == 1:
                c, d, e = stream[pos:pos+3]; pos += 3
                length, distance = ((b & 15) << 12 | c << 4 | d >> 4)+0x111, ((d & 15) << 8 | e)+1
            else:
                c = stream[pos]; pos += 1
                length, distance = hi+1, ((b & 15) << 8 | c)+1
            assert 0 < distance <= len(out) and len(out)+length <= size, 'LZ11 range'
            for _ in range(length): out.append(out[-distance])
    assert not any(stream[pos:]), 'nonzero LZ11 padding'
    return bytes(out), data[audio:]


def textures(data):
    result=[]
    for p in range(0,len(data)-60,4):
        if data[p:p+8] != b'\x11\x00\x00\x20TXOB': continue
        height,width=struct.unpack_from('<II',data,p+24)
        fmt=struct.unpack_from('<I',data,p+52)[0]
        assert fmt in (3,4,5), 'unsupported HOME texture format (RGBA8 forbidden)'
        assert 8 <= width <= 512 and 8 <= height <= 256, 'texture dimensions'
        assert width & (width-1)==0 and height & (height-1)==0, 'texture is not power of two'
        image=p+56+struct.unpack_from('<i',data,p+56)[0]
        assert 0 <= image <= len(data)-32, 'image pointer'
        ih,iw,size,rel=struct.unpack_from('<IIIi',data,image)
        start=image+12+rel
        assert (iw,ih)==(width,height) and size==width*height*2, 'texture byte count'
        assert 0 <= start and start+size <= len(data), 'texture data bounds'
        result.append(dict(width=width,height=height,format=fmt,bytes=size))
    assert len(result)==3, 'expected exactly three textures'
    assert sum(t['bytes'] for t in result)<=224*1024, 'texture budget'
    return result


def audio_info(data):
    assert data[:4]==b'CWAV' and data[4:6]==b'\xff\xfe', 'CWAV header'
    assert struct.unpack_from('<I',data,12)[0]==len(data), 'CWAV length'
    info=struct.unpack_from('<I',data,24)[0]
    start=struct.unpack_from('<I',data,36)[0]
    assert data[info:info+4]==b'INFO' and data[start:start+4]==b'DATA', 'CWAV blocks'
    assert data[info+8]==1 and data[info+9]==0, 'PCM16, nonlooping audio required'
    rate,loop,end,unused,channels=struct.unpack_from('<5I',data,info+12)
    assert rate==32000 and loop==0 and end==96000 and channels==2, 'audio must be 3 seconds stereo 32 kHz'
    samples=[]
    for i in range(channels):
        ref=info+32+8*i
        ch=info+28+struct.unpack_from('<i',data,ref+4)[0]
        offset=start+8+struct.unpack_from('<I',data,ch+4)[0]
        assert 0 <= offset and offset+end*2<=len(data), 'audio samples out of bounds'
        samples.append(data[offset:offset+end*2])
    return dict(seconds=end/rate,channels=channels,rate=rate,loop=False),samples


def verify_icon(data):
    assert len(data)==0x36c0 and data[:4]==b'SMDH', 'invalid SMDH icon'
    flags=struct.unpack_from('<I',data,0x2028)[0]
    assert flags & 0x20, 'CIA icon is missing extendedbanner (0x20)'
    return flags


def verify_cia(path,expected):
    data=Path(path).read_bytes()
    header,_,_,cert,ticket,tmd,meta,content=struct.unpack_from('<IHHIIIIQ',data)
    align=lambda n:(n+63)&~63
    base=align(align(align(align(header)+cert)+ticket)+tmd)
    assert base+content<=len(data) and data[base+0x100:base+0x104]==b'NCCH', 'CIA content'
    offset,size=struct.unpack_from('<II',data,base+0x1a0)
    exe=base+offset*512
    assert exe+size*512<=base+content and size>0, 'ExeFS bounds'
    assert hashlib.sha256(data[exe:exe+512]).digest()==data[base+0x1c0:base+0x1e0], 'ExeFS header hash'
    files={}
    for i in range(10):
        name,off,length=struct.unpack_from('<8sII',data,exe+i*16)
        if not name.strip(b'\0'):continue
        name=name.rstrip(b'\0').decode();a=exe+512+off;b=a+length
        assert exe+512<=a<=b<=exe+size*512, 'ExeFS file bounds'
        payload=data[a:b]
        assert hashlib.sha256(payload).digest()==data[exe+0x1e0-i*32:exe+0x200-i*32], 'ExeFS file hash'
        files[name]=payload
    assert files['banner']==expected, 'CIA contains a different banner'
    icon_flags=verify_icon(files['icon'])
    return dict(cia_sha256=hashlib.sha256(data).hexdigest(),exefs_hashes_verified=sorted(files),banner_matches=True,icon_flags=icon_flags)


if __name__ == '__main__':
    import json
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('path', type=Path)
    ap.add_argument('--cia',type=Path,help='Verify ExeFS hashes and the banner actually included in this CIA')
    ap.add_argument('--require-static',action='store_true',help='Reject all animation resources for production recovery')
    a = ap.parse_args()
    data = a.path.read_bytes()
    audio=None
    if data[:4] == b'CBMD':
        data, sound = unpack_banner(data);audio,_=audio_info(sound)
    if a.require_static:require_static(data)
    report=inspect(data);report['textures']=textures(data);report['audio']=audio
    if a.cia:report['package']=verify_cia(a.cia,a.path.read_bytes())
    print(json.dumps(report, indent=2))
