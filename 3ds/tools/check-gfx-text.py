#!/usr/bin/env python3
"""Host sanitizer check of the actual gfx.c text implementation, with GPU stubs.
Does not compile/link the application or claim console validation.
"""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
GEN=ROOT/'3ds/build/gen'
s=(ROOT/'3ds/source/gfx.c').read_text()
core=s[s.index('// ---- text:'):s.index('// ---- shapes:')]
core=core.replace('romfs:/gfx/','../../romfs/gfx/')
hashfn=s[s.index('static u32 name_hash'):s.index('static void ui_map_build')]
header=r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>
typedef uint8_t u8; typedef int8_t s8; typedef uint16_t u16; typedef int16_t s16;
typedef uint32_t u32; typedef int32_t s32;
typedef struct {int w,h; int tex; bool crisp,ready;} Sheet;
typedef struct {u16 w,h; float l,t,r,b;} Tex3DS_SubTexture;
typedef struct {int *tex; Tex3DS_SubTexture *sub;} C2D_Image;
typedef struct {int dummy;} C2D_ImageTint;
static float DRAW_X, DRAW_Y, draw_x, draw_y;
enum {FONT_UI=0,FONT_BRAND=1,FONT_MONO=2,FONT_WEIGHT_BODY=500,FONT_WEIGHT_LABEL=600};
enum {AL_LEFT,AL_CENTER,AL_RIGHT};
static bool sheet_ok(Sheet *s){return s->ready;}
static void sheet_free(Sheet *s){s->ready=false;}
static bool sheet_load_now(Sheet *s,const char *p){
 FILE *f=fopen(p,"rb");u8 h[12]; if(!f)return false;
 bool ok=fread(h,1,12,f)==12;fclose(f);s->w=h[4]|h[5]<<8;s->h=h[6]|h[7]<<8;return s->ready=ok;
}
static void C2D_DrawImageAt(C2D_Image i,float x,float y,float z,const C2D_ImageTint *t,float sx,float sy){draw_x=x;draw_y=y;}
static void C2D_PlainImageTint(C2D_ImageTint *t,u32 c,float a){}
'''
tests=r'''
int main(void){
 assert(fonts_load());
 assert(npages>=3 && nstyles>=86);
 assert(font_style(10,600,FONT_MONO)->family==FONT_MONO);
 assert(text_style_w("iiii",10,600,FONT_MONO)==text_style_w("WWWW",10,600,FONT_MONO));
 const Style *wordmark=font_style(30,800,1);assert(wordmark->family==1&&wordmark->size==30&&wordmark->weight==800&&wordmark->line==30);
 assert(line_h(9)==12 && line_h(10)==14 && line_h(11)==15 && line_h(15)==18);
 const Style *st=font_style(10,600,0);assert(st->size==10 && st->weight==600 && st->line==14);
 assert(font_style(10,750,1)->family==1);
 const char *unknown="\xf4\x8f\xbf\xbd", *replacement="\xef\xbf\xbd";
 assert(text_w(unknown,11)==text_w(replacement,11) && text_w(unknown,11)>0);
 assert(text_wrap_h("A\xf4\x8f\xbf\xbd B",11,12)==text_wrap_h("A\xef\xbf\xbd B",11,12));
 text("A",10,20,11,0xffffffff,AL_LEFT);float x=draw_x,y=draw_y;
 DRAW_X=12;DRAW_Y=5;text("A",10,20,11,0xffffffff,AL_LEFT);
 assert(draw_x==x+12 && draw_y==y+5);
 size_t page=(size_t)sysconf(_SC_PAGESIZE);
 char *buf=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(buf!=MAP_FAILED && !mprotect(buf+page,page,PROT_NONE));
 const char *bad[]={"\x80","\xff","\xc0\xaf","\xe0\x80\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xf0\x90","\xe2\x82"};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){
  size_t n=strlen(bad[i])+1;char *at=buf+page-n;memcpy(at,bad[i],n);
  u32 cp;assert(utf8(at,&cp)==1 && cp==0xfffd);assert(text_w(at,11)>0);
  assert(text_wrap_h(at,11,7)>0);
 }
 munmap(buf,page*2);
 // Offsets beyond 65535 bytes must remain intact when wrapping remote text.
 char *large=malloc(70002);memset(large,'A',70001);large[70001]=0;
 WEntry *e=wrap_get(large,font_style(11,500,0),200);
 assert(e->end[e->lines-1]==70001);free(large);
 assert(fabsf(text_style_w("Downloads",10,600,0)-53.95f)<.1f);
 assert(kern(font_style(14,700,0),'A','V')<0);
 text_reset();kerning_free();free(glyphs);glyphs=NULL;nstyles=0;
 assert(text_w("unavailable font",11)==0); // no atlas must not dereference NULL
 puts("PASS: FNT3 loading, exact metrics, remote replacement width/wrap, UTF-8 guard-page cases, >64K offsets, text XY translation, missing atlas");
 return 0;
}
'''
path=GEN/'gfx-text-check.c';path.write_text(header+hashfn+core+tests)
subprocess.run(['cc','-std=gnu11','-fsanitize=address,undefined','-g',str(path),'-lm','-o',str(GEN/'gfx-text-check')],check=True)
r=subprocess.run([str(GEN/'gfx-text-check')],cwd=GEN,text=True,capture_output=True)
(GEN/'gfx-text-check.log').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr,end='');r.check_returncode()
