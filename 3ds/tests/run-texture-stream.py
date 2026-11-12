#!/usr/bin/env python3
"""Production texture-stream.inc with real pthread handoff and fake GPU storage."""
from pathlib import Path
import subprocess,tempfile,struct,zlib
root=Path(__file__).resolve().parents[1]
sdk=r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;
typedef pthread_mutex_t LightLock;
typedef struct{pthread_mutex_t mutex;pthread_cond_t cond;bool set;}LightEvent;
typedef void *Thread;
static inline void LightLock_Init(LightLock*p){pthread_mutex_init(p,NULL);}
static inline void LightLock_Lock(LightLock*p){pthread_mutex_lock(p);}
static inline void LightLock_Unlock(LightLock*p){pthread_mutex_unlock(p);}
static inline void LightEvent_Init(LightEvent*p,int type){(void)type;pthread_mutex_init(&p->mutex,NULL);pthread_cond_init(&p->cond,NULL);p->set=false;}
static inline void LightEvent_Signal(LightEvent*p){pthread_mutex_lock(&p->mutex);p->set=true;pthread_cond_signal(&p->cond);pthread_mutex_unlock(&p->mutex);}
static inline void LightEvent_Wait(LightEvent*p){pthread_mutex_lock(&p->mutex);while(!p->set)pthread_cond_wait(&p->cond,&p->mutex);p->set=false;pthread_mutex_unlock(&p->mutex);}
static inline u64 osGetTime(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000ull+t.tv_nsec/1000000;}
'''
gpu=r'''
#pragma once
#include "3ds.h"
#include <stdlib.h>
typedef enum{GPU_RGBA8,GPU_RGB565,GPU_RGBA4,GPU_A8,GPU_RGB8}GPU_TEXCOLOR;
enum{GPU_NEAREST,GPU_LINEAR,GPU_CLAMP_TO_EDGE};
typedef struct{void*data;}C3D_Tex;
static inline bool C3D_TexInit(C3D_Tex*t,int w,int h,GPU_TEXCOLOR fmt){static const unsigned bpp[]={4,2,2,1,3};t->data=malloc(w*h*bpp[fmt]);return t->data!=NULL;}
static inline void C3D_TexDelete(C3D_Tex*t){free(t->data);t->data=NULL;}
static inline void C3D_TexSetFilter(C3D_Tex*t,int a,int b){(void)t;(void)a;(void)b;}
static inline void C3D_TexSetWrap(C3D_Tex*t,int a,int b){(void)t;(void)a;(void)b;}
static inline void C3D_TexFlush(C3D_Tex*t){(void)t;}
'''
with tempfile.TemporaryDirectory(prefix='forge-texture-') as temp:
 p=Path(temp);(p/'3ds.h').write_text(sdk);(p/'citro3d.h').write_text(gpu)
 (p/'citro2d.h').write_text('''#pragma once
#include "citro3d.h"
typedef struct{int width,height;float left,right,top,bottom;}Tex3DS_SubTexture;
typedef struct{C3D_Tex*tex;Tex3DS_SubTexture*subtex;}C2D_Image;
static inline u32 C2D_Color32(u8 r,u8 g,u8 b,u8 a){return r|(u32)g<<8|(u32)b<<16|(u32)a<<24;}
''')
 raw=bytes(range(256));packed=zlib.compress(raw)
 sheet=struct.pack('<4sHHBBH4HII',b'FTX1',8,8,0,1,0,0,0,8,8,len(raw),len(packed))+packed
 (p/'sheet.ftx').write_bytes(sheet);(p/'romfs:/gfx').mkdir(parents=True)
 bank=struct.pack('<4sII64sII',b'FBN1',1,84+len(sheet),b'demo-bg.ftx',84,len(sheet))+sheet
 (p/'romfs:/gfx/banner-bank.bin').write_bytes(bank)
 for sanitizer in ('address,undefined','thread'):
  exe=p/('test-'+sanitizer.replace(',','-'))
  subprocess.run(['clang','-std=gnu11','-O1','-g','-fsanitize='+sanitizer,'-I'+str(p),'-I/opt/homebrew/include','-I'+str(root/'source'),str(root/'tests/texture-stream.c'),'-lpthread','-lz','-o',str(exe)],check=True)
  subprocess.run([str(exe)],cwd=p,check=True,timeout=15)
