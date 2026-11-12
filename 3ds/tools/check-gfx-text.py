#!/usr/bin/env python3
"""Host sanitizer check of the actual gfx.c text implementation, with GPU stubs.
Does not compile/link the application or claim console validation.
"""
from pathlib import Path
import subprocess
import json
ROOT=Path(__file__).resolve().parents[2]
GEN=ROOT/'3ds/build/gen'
s=(ROOT/'3ds/source/gfx.c').read_text()
core=s[s.index('// ---- text:'):s.index('// ---- shapes:')]
core=core.replace('romfs:/gfx/','../../romfs/gfx/')
scenes=(ROOT/'3ds/source/scenes.c').read_text()
fitting=scenes[scenes.index('static void fit_label_style('):scenes.index('static void fit_label(char')]
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
static struct {float x,y; int w;} samples[16384];
static int sample_count;
static bool capture;
enum {FONT_UI=0,FONT_BRAND=1,FONT_MONO=2,FONT_WEIGHT_BODY=500,FONT_WEIGHT_LABEL=600};
enum {AL_LEFT,AL_CENTER,AL_RIGHT};
static bool sheet_ok(Sheet *s){return s->ready;}
static void sheet_free(Sheet *s){s->ready=false;}
static bool sheet_load_now(Sheet *s,const char *p){
 FILE *f=fopen(p,"rb");u8 h[12]; if(!f)return false;
 bool ok=fread(h,1,12,f)==12;fclose(f);s->w=h[4]|h[5]<<8;s->h=h[6]|h[7]<<8;return s->ready=ok;
}
static void C2D_DrawImageAt(C2D_Image i,float x,float y,float z,const C2D_ImageTint *t,float sx,float sy){
 draw_x=x;draw_y=y;
 if(capture){assert(sample_count<16384);samples[sample_count++]=(typeof(samples[0])){x,y,i.sub->w};}
}
static void C2D_PlainImageTint(C2D_ImageTint *t,u32 c,float a){}
'''
tests=r'''
#include "typography-corpus.inc"
static unsigned long checked_pairs, checked_lines, checked_strings;
static void check_ink(const Style *st,const char *s,float spacing){
 sample_count=0;capture=true;
 text_ex(s,10,20,st->size,st->weight,st->family,spacing,0xffffffff,AL_LEFT);
 capture=false;
 int count=0,right=-100000,extent=0;
 for(const char *p=s;*p;){
  u32 cp;p+=utf8(p,&cp);if(cp=='\n')break;
  if(cp=='\r'||cp==0x200d||(cp>=0xfe00&&cp<=0xfe0f))continue;
  const Glyph *g=glyph(st,cp);if(!g||!g->w)continue;
  assert(count<sample_count);
  int left=(int)samples[count].x+(g->ink_insets&15);
  if(g->adv)assert(left>=right+1);
  int r=(int)samples[count].x+g->w-(g->ink_insets>>4);
  if(r>right)right=r;
  int end=(int)samples[count].x+g->w;if(end>extent)extent=end;
  count++;
 }
 assert(count==sample_count);
 assert(extent<=10+ceilf(text_ex_w(s,st->size,st->weight,st->family,spacing)));
 checked_strings++;
}
static void check_wrapping(const char *s,const Style *st,float width){
 WEntry *e=wrap_get(s,st,width);
 for(int i=0;i<e->lines;i++){
  const char *start=s+e->begin[i],*end=s+e->end[i];u32 cp;int n=utf8(start,&cp);
  float actual=run(st,start,end,0,0,NULL);
  // A single glyph wider than the viewport must still make forward progress.
  if(end-start>n&&actual>width+.001f){
   fprintf(stderr,"wrap overflow size=%.0f weight=%d width=%.2f actual=%.2f line=%.*s\n",st->size,st->weight,width,actual,(int)(end-start),start);
   assert(actual<=width+.001f);
  }
  checked_lines++;
 }
}
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
 // Original font metrics are preserved only for the approved right status role.
 assert(fabsf(text_status_w("16:25",12)-31.40625f)<.001f);
 assert(fabsf(text_status_w("82%",12)-27.21875f)<.001f);
 assert(kern(font_style(14,700,0),'A','V')<0);
 assert(glyph(font_style(15,400,FONT_BRAND),0x8a73)->cp==0x8a73); // 詳: native section fallback
 DRAW_X=DRAW_Y=0;
 for(int si=0;si<nstyles;si++){
  const Style *f=&styles[si];if(f->kind)continue;
  // Every printable ASCII pair, also following a prefix to exercise fractional phases.
  for(int a=33;a<127;a++)for(int b=33;b<127;b++){
   char pair[]={(char)a,(char)b,0}, prefixed[]={'m',(char)a,(char)b,0};
   check_ink(f,pair,0);check_ink(f,prefixed,-.5f);checked_pairs+=2;
  }
  check_ink(f,"EstebanPdN · SNES → 3DS · forge.store · instalación · Français · Русский · 日本語",0);
 }
 for(unsigned i=0;i<sizeof corpus/sizeof *corpus;i++){
  const int sizes[]={9,10,11,14},weights[]={500,600,500,700};
  for(int role=0;role<4;role++){
   const Style *f=font_style(sizes[role],weights[role],0);
   check_ink(f,corpus[i],0);
   for(int width=80;width<=280;width+=100)check_wrapping(corpus[i],f,width);
   for(int width=16;width<=216;width+=100){
    char fitted[96];fit_label_style(fitted,sizeof fitted,corpus[i],sizes[role],weights[role],width);
    assert(text_style_w(fitted,sizes[role],weights[role],0)<=width+.001f);
   }
  }
  sample_count=0;capture=true;text_caps_fit(corpus[i],10,20,9,.3f,0xffffffff,113);capture=false;
  for(int j=0;j<sample_count;j++)assert(samples[j].x+samples[j].w<=123);
 }
 printf("PASS: %lu pair/phase cases, %lu strings, %lu wrapped lines; visible ink clearance >= 1 px, widths contain drawn glyphs\n",checked_pairs,checked_strings,checked_lines);
 text_reset();kerning_free();free(glyphs);glyphs=NULL;nstyles=0;
 assert(text_w("unavailable font",11)==0); // no atlas must not dereference NULL
 puts("PASS: FNT3 loading, exact metrics, remote replacement width/wrap, UTF-8 guard-page cases, >64K offsets, text XY translation, missing atlas");
 return 0;
}
'''
corpus=set()
def collect(value):
    if isinstance(value,str) and value and len(value)<8192: corpus.add(value)
    elif isinstance(value,dict):
        for v in value.values(): collect(v)
    elif isinstance(value,list):
        for v in value: collect(v)
for name in ['3ds/romfs/strings.json','catalog/forge-store.json','3ds/romfs/universal.json']:
    collect(json.loads((ROOT/name).read_text()))
# Byte escapes avoid changing UTF-8, newlines, quotes or C escape boundaries.
def cstr(s):return '"'+''.join('\\%03o'%b for b in s.encode())+'"'
(GEN/'typography-corpus.inc').write_text('static const char *corpus[]={\n'+',\n'.join(map(cstr,sorted(corpus)))+'\n};\n')
path=GEN/'gfx-text-check.c';path.write_text(header+hashfn+core+fitting+tests)
subprocess.run(['cc','-std=gnu11','-fsanitize=address,undefined','-g',str(path),'-lm','-o',str(GEN/'gfx-text-check')],check=True)
r=subprocess.run([str(GEN/'gfx-text-check')],cwd=GEN,text=True,capture_output=True)
(GEN/'gfx-text-check.log').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr,end='');r.check_returncode()
