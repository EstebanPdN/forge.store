#!/usr/bin/env python3
"""Test the production README worker with deterministic transport fixtures."""
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[2];s=(root/'3ds/source/network.c').read_text();source=s[s.index('typedef struct {',s.index('#define READER_LIMIT')):s.index('bool reader_slot_busy')]
shim=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>
typedef void *Thread;typedef uint32_t u32;typedef uint64_t u64;
#define READER_SLOTS 5
#define READER_LIMIT 1048576
static int mode,raw_calls,api_calls;static u64 clock_ms=100;
static u64 osGetTime(void){return clock_ms;}
static bool http_cache_fresh(const char*u,const char*a,u32 ttl){return true;}
static bool http_cache_bundled(const char*u,const char*a){return mode==1;}
static char *http_cache_get(const char*u,const char*a,size_t cap,bool refresh,bool(*cancel)(void*),void*ctx,size_t*n,char*e,size_t z){
 e[0]=0;if(cancel(ctx))return NULL;
 if(strstr(u,"raw.githubusercontent.com")){raw_calls++;assert(!strcmp(u,"https://raw.githubusercontent.com/example/project/HEAD/README.md"));assert(!strcmp(a,"text/plain"));if(mode==2){snprintf(e,z,"404");return NULL;}}
 else{api_calls++;assert(!strcmp(u,"https://api.github.com/repos/example/project/readme"));}
 *n=9;return strdup("# Fixture");
}
'''
main=r'''
int main(void){
 for(mode=0;mode<4;mode++){
  raw_calls=api_calls=0;ReaderRequest r={.raw=true,.deadline=mode==3?99:1000};strcpy(r.path,"repos/example/project/readme");read_thread(&r);
  assert(r.ready);if(mode==3){assert(!r.body&&!raw_calls&&!api_calls);}else{assert(r.body&&!strcmp(r.body,"# Fixture")&&!r.error[0]);assert(raw_calls==(mode==1?0:1));assert(api_calls==(mode==0?0:1));}free(r.body);
 }
 puts("PASS: actual read_thread: raw-first, bundled-first, nonstandard fallback, deadline and error clearing.");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(shim+source+main);subprocess.run(['clang','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
