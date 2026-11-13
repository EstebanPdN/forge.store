#!/usr/bin/env python3
"""Read-only inventory of CGFX skeletons and CANM members; not hardware certification."""
import argparse, hashlib, json, struct
from pathlib import Path
from home_banner_validate import dictionaries


def audit(data):
    def u32(p): return struct.unpack_from('<I',data,p)[0]
    def ptr(p):
        v=struct.unpack_from('<i',data,p)[0]
        if not v:return None
        q=p+v
        if not 0<=q<len(data):raise ValueError('out-of-range pointer')
        return q
    def name(p):
        q=ptr(p)
        return data[q:data.index(b'\0',q)].decode() if q is not None else ''
    bones=[]
    for rows in dictionaries(data):
        if not any(r[3].startswith(b'Scene root') for r in rows):continue
        for _,_,_,key,p in rows[1:]:
            try: own=name(p)
            except (ValueError,UnicodeDecodeError):continue
            if own!=key.decode():continue
            bones.append(dict(name=own, id=u32(p+8),parent=name(ptr(p+16)) if ptr(p+16) is not None else '',billboard=u32(p+212),trs=struct.unpack_from('<9f',data,p+32)))
    animations=[]
    for p in range(0,len(data)-40,4):
        if data[p:p+4]!=b'CANM':continue
        table=ptr(p+28);members=[]
        if table is not None:
            for i in range(1,u32(table+8)+1):
                m=ptr(table+12+16*i+12)
                members.append(dict(name=name(m+4),primitive=u32(m+16),flags=hex(u32(m))))
        animations.append(dict(name=name(p+8),group=name(p+12),frames=struct.unpack_from('<f',data,p+20)[0],members=members))
    return dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),bones=bones,animations=animations)

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('files',type=Path,nargs='+');a=ap.parse_args()
    print(json.dumps({str(p):audit(p.read_bytes()) for p in a.files},indent=2))
