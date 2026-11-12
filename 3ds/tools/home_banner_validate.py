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
        q = p + struct.unpack_from('<i', data, p)[0]
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
                if slot == 7: assert all(v == 0 for v in values), 'vertical bob forbidden'
            if members[-1] == 'Medallion':
                yaw = tracks[4]; xs = tracks[6]; zs = tracks[8]
                assert len(yaw) == len(xs) == len(zs) and min(yaw) < -.4 and max(yaw) > .4, 'missing lateral swing'
                for angle, x, z in zip(yaw, xs, zs):
                    assert abs(-8.2*math.cos(angle)+.7*math.sin(angle)+x+8.2) < 1e-5, 'coin pivot drifts horizontally'
                    assert abs(8.2*math.sin(angle)+.7*math.cos(angle)+z-.7) < 1e-5, 'coin pivot drifts in depth'
        result.append(dict(frames=duration, members=members, curves=curve_count, scales_positive=True, rotation_tracks=rotation_tracks))
    return result


def inspect(data):
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
            bones[key.decode()] = dict(mode=u32(p+212), trs=struct.unpack_from('<9f', data, p+32))
    meshes = []
    for p in range(0, len(data)-116, 4):
        if data[p:p+8] != b'\0\0\0\x01SOBJ':
            continue
        target = name(p+112)
        assert target in bones, ('mesh has no resolved bone', target, hex(p))
        b = bones[target]
        assert b['mode'] == (0 if target == 'Medallion' else 5), ('unexpected billboard mode', target, b['mode'])
        assert b['trs'] == (1.,1.,1.,0.,0.,0.,0.,0.,0.), ('non-identity rest bone', target, b['trs'])
        meshes.append(target)
    assert meshes, 'no meshes'
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                dictionaries=len(tables), dictionary_keys=sum(len(t)-1 for t in tables),
                meshes=len(meshes), rendered_bones=sorted(set(meshes)),
                skeletal_animations=animations(data),
                all_mesh_links_resolve=True, medallion_billboard_off=True,
                all_rendered_rest_transforms_identity=True, hardware_tested=False)


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
    return dict(cia_sha256=hashlib.sha256(data).hexdigest(),exefs_hashes_verified=sorted(files),banner_matches=True)


if __name__ == '__main__':
    import json
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('path', type=Path)
    ap.add_argument('--cia',type=Path,help='Verify ExeFS hashes and the banner actually included in this CIA')
    a = ap.parse_args()
    data = a.path.read_bytes()
    audio=None
    if data[:4] == b'CBMD':
        data, sound = unpack_banner(data);audio,_=audio_info(sound)
    report=inspect(data);report['textures']=textures(data);report['audio']=audio
    if a.cia:report['package']=verify_cia(a.cia,a.path.read_bytes())
    print(json.dumps(report, indent=2))
