#!/usr/bin/env python3
"""Exercise the current production tab geometry with SDK drawing/metric stubs."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[2];scenes=(root/'3ds/source/scenes.c').read_text()
curve=re.search(r'float bezier\([^\n]*\)\s*\{[\s\S]*?\n\}',scenes);assert curve
core=scenes[scenes.index('// Shared V2 TabBar.'):scenes.index('static void tabs_draw')]
header='''#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>
static float NOW,DRAW_X;
static float clampf(float x,float a,float b){return x<a?a:x>b?b:x;}
static float lerpf(float a,float b,float t){return a+(b-a)*t;}
static float span(float now,float start,float duration){return clampf((now-start)/duration,0,1);}
static float text_style_w(const char *s,float size,int weight,int family){return strlen(s)*5.125f;}
'''
cases=(root/'3ds/tests/tabs-cases.inc').read_text();gen=root/'3ds/build/gen/ui-check';gen.mkdir(exist_ok=True)
source=gen/'tabs-current.c';source.write_text(header+curve[0]+core+cases)
with tempfile.TemporaryDirectory(prefix='forge-tabs-') as temp:
 exe=Path(temp)/'test';subprocess.run(['cc','-std=gnu11','-g','-fsanitize=address,undefined',str(source),'-lm','-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True);(gen/'tabs-current.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,end='');result.check_returncode()
