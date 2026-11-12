#!/usr/bin/env python3
"""Bounded decoded-image cache; actual production source with host SHA adapter."""
import pathlib,subprocess,tempfile,zlib,struct
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='reader-image-test-') as tmp:
 p=pathlib.Path(tmp);(p/'sdmc:/3ds/forge-store').mkdir(parents=True)
 subprocess.run(['clang','-g','-O1','-std=gnu11','-fsanitize=address,undefined','-I/opt/homebrew/include',str(root/'tests/reader-image.c'),'-L/opt/homebrew/lib','-lcrypto','-lz','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True)
count=0
for p in (root/'romfs/reader-art').glob('*.rimg'):
 data=p.read_bytes();magic,w,h,raw,size,crc=struct.unpack_from('<4sHHIII',data)
 assert magic==b'RIM1' and 0<w<=376 and 0<h<=240 and raw==w*h*4 and size==len(data)-20
 pixels=zlib.decompress(data[20:]);assert len(pixels)==raw and zlib.crc32(pixels)==crc
 count+=1
print(f'PASS: {count} bundled README images have valid sizes and CRCs')
