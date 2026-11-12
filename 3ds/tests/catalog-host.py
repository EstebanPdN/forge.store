#!/usr/bin/env python3
"""Production catalog host tests. Only SDK surfaces are stubbed; JSON/files are real."""
from pathlib import Path
import os, subprocess, tempfile, shutil
root = Path(__file__).resolve().parents[2]
jansson = Path(os.environ.get('JANSSON_PREFIX', '/opt/homebrew/opt/jansson'))
with tempfile.TemporaryDirectory(prefix='catalog-host-') as tmp:
    work = Path(tmp)
    inc = work / 'include'
    inc.mkdir()
    (inc / '3ds.h').write_text('''#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64; typedef int32_t s32;
typedef int LightLock;
#define CUR_THREAD_HANDLE 0
#define CFG_LANGUAGE_EN 1
#define CFG_LANGUAGE_ES 2
#define CFG_LANGUAGE_PT 3
#define CFG_LANGUAGE_IT 4
#define CFG_LANGUAGE_FR 5
#define CFG_LANGUAGE_DE 6
#define CFG_LANGUAGE_RU 7
#define CFG_LANGUAGE_JP 8
static inline void LightLock_Init(LightLock *l) {*l=0;}
void LightLock_Lock(LightLock *l);
void LightLock_Unlock(LightLock *l);
void CFGU_GetSystemLanguage(u8 *l);
void svcGetThreadPriority(s32 *p, int h);
void svcSleepThread(int64_t ns);
static inline size_t linearSpaceFree(void){return 16*1024*1024;}
void *threadCreate(void (*fn)(void *), void *arg, size_t stack, int prio, int affinity, bool detached);
''')
    (inc / 'citro3d.h').write_text('#pragma once\ntypedef struct {int unused;} C3D_Tex;\n')
    (inc / 'citro2d.h').write_text('''#pragma once
#include "3ds.h"
#include "citro3d.h"
typedef struct {int unused;} Tex3DS_SubTexture;
typedef struct {C3D_Tex *tex; Tex3DS_SubTexture *subtex;} C2D_Image;
static inline u32 C2D_Color32(u8 r,u8 g,u8 b,u8 a) {return r | (u32)g<<8 | (u32)b<<16 | (u32)a<<24;}
''')
    (work / 'sdmc:/3ds/forge-store').mkdir(parents=True)
    (work / 'romfs:/gfx').mkdir(parents=True)
    for name in ('catalog.json', 'universal.json'):
        shutil.copyfile(root / '3ds/romfs' / name, work / 'romfs:' / name)
    exe = work / 'catalog-host'
    subprocess.run(['clang', '-std=gnu11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
        '-Wno-unused-parameter', '-Wno-missing-field-initializers',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I'+str(inc), '-I'+str(jansson/'include'), '-I'+str(root/'3ds/source'),
        str(root/'3ds/tests/catalog-host.c'), '-DROM_FIND_HOST_TEST', str(root/'3ds/source/rom_find.c'), '-L'+str(jansson/'lib'), '-ljansson', '-lz', '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], cwd=work, check=True)
