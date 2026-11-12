#!/usr/bin/env python3
"""The production music reader: exact PCM, opening/loop seams, restart and errors."""
from pathlib import Path
import gzip,importlib.util,subprocess,tempfile,struct
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('music_pack',root/'tools/music-pack.py')
packer=importlib.util.module_from_spec(spec);spec.loader.exec_module(packer)
source=r'''
#include "music_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *load(const char *path,size_t *n){
 FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);
 unsigned char*p=malloc(*n);assert(p);assert(fread(p,1,*n,f)==*n);fclose(f);return p;
}
int main(int argc,char **argv){
 assert(argc==3);size_t a,b;unsigned char *opening=load(argv[1],&a),*loop=load(argv[2],&b);
 unsigned char block[16384];MusicStream s={0};
 for(int restart=0;restart<2;restart++){
  assert(music_stream_open(&s,"start.gz","loop.gz"));
  for(size_t pos=0;pos<a+2*b;pos+=sizeof block){
   assert(music_stream_read(&s,block,sizeof block));
   for(size_t i=0;i<sizeof block;i++){
    size_t at=pos+i;assert(block[i]==(at<a?opening[at]:loop[(at-a)%b]));
   }
  }
 }
 music_stream_close(&s);music_stream_close(&s);
 assert(!music_stream_open(&s,"missing.gz","loop.gz"));
 const char *bad[]={"empty.gz","truncated.gz","bad-crc.gz","bad-rate.gz","odd-size.gz","trailing.gz"};
 for(unsigned k=0;k<sizeof bad/sizeof bad[0];k++){
  bool failed=!music_stream_open(&s,bad[k],"loop.gz");
  for(size_t pos=0;!failed&&pos<a+32768;pos+=sizeof block)if(!music_stream_read(&s,block,sizeof block)){failed=true;break;}
  assert(failed);music_stream_close(&s);
 }
 assert(music_stream_open(&s,"start.gz","missing.gz"));
 bool failed=false;for(size_t pos=0;!failed&&pos<a+32768;pos+=sizeof block)if(!music_stream_read(&s,block,sizeof block)){failed=true;break;}
 assert(failed);music_stream_close(&s);free(opening);free(loop);
 puts("PASS: sample-identical music, opening/loop seams, two loops, restart, CRC, truncation, empty and missing files; ASan/UBSan");
}
'''
with tempfile.TemporaryDirectory(prefix='forge-music-') as temp:
 p=Path(temp);start=root/'romfs/audio/music-start.pcm';loop=root/'romfs/audio/music.pcm'
 packed=packer.encode(start.read_bytes())
 (p/'start.gz').write_bytes(packed);(p/'loop.gz').write_bytes(packer.encode(loop.read_bytes()))
 (p/'empty.gz').write_bytes(gzip.compress(b'',mtime=0));(p/'truncated.gz').write_bytes(packed[:-19])
 corrupt=bytearray(packed);corrupt[-8]^=1;(p/'bad-crc.gz').write_bytes(corrupt)
 (p/'bad-rate.gz').write_bytes(gzip.compress(struct.pack('<4sII',b'FPC2',4,44100)+b'\0'*4,mtime=0))
 (p/'odd-size.gz').write_bytes(gzip.compress(struct.pack('<4sII',b'FPC2',3,32000)+b'\0'*3,mtime=0))
 (p/'trailing.gz').write_bytes(gzip.compress(gzip.decompress(packed)+b'extra',mtime=0))
 (p/'test.c').write_text(source)
 subprocess.run(['clang','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-I'+str(root/'source'),str(p/'test.c'),'-lz','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(start),str(loop)],cwd=p,check=True)
