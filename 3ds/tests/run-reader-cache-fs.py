#!/usr/bin/env python3
"""Production cache replacement on a filesystem that cannot overwrite by rename."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int fail_promotion;
static int fat_rename(const char *from,const char *to){
 if(!access(to,F_OK)){errno=EEXIST;return -1;}
 if(fail_promotion&&strstr(from,".tmp")){errno=EIO;return -1;}
 return rename(from,to);
}
#define rename fat_rename
#include "reader_cache.c"
#undef rename
int main(void){
 size_t n;char*p;
 assert(reader_cache_save("cache",100,"old",3,100));
 assert(reader_cache_save("cache",101,"new",3,100));
 p=reader_cache_load("cache",102,60,100,&n);assert(p&&n==3&&!strcmp(p,"new"));free(p);
 fail_promotion=1;assert(!reader_cache_save("cache",103,"broken",6,100));
 p=reader_cache_load("cache",104,60,100,&n);assert(p&&n==3&&!strcmp(p,"new"));free(p);
 assert(access("cache.tmp",F_OK)!=0);assert(access("cache.bak",F_OK)!=0);
 puts("PASS: FAT cache replacement and failed-promotion recovery preserve the last complete response; ASan/UBSan");
}
'''
with tempfile.TemporaryDirectory(prefix='forge-cache-fs-') as temp:
 p=Path(temp);(p/'test.c').write_text(source)
 subprocess.run(['clang','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-I'+str(root/'source'),str(p/'test.c'),'-lz','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True)
