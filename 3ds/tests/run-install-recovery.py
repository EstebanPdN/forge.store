#!/usr/bin/env python3
"""Compiles production install.c with the power-loss harness; sdmc: is a fresh temp dir."""
import pathlib, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='forge-recovery-test-') as temp:
    p=pathlib.Path(temp)
    header=(root/'source/install.h').read_text().replace('#include "catalog.h"','')
    (p/'install-types.h').write_text(header)
    (p/'malloc.h').write_text('#include <stdlib.h>\nstatic void *memalign(size_t a,size_t n){void*p=0;return posix_memalign(&p,a,n)?0:p;}\n')
    cmd=['clang','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-Wno-deprecated-declarations','-Wno-format','-I'+temp,'-I/opt/homebrew/include','-I/opt/homebrew/opt/libarchive/include',str(root/'tests/install-recovery.c'),'-L/opt/homebrew/lib','-L/opt/homebrew/opt/libarchive/lib','-ljansson','-larchive','-lcurl','-o',str(p/'test')]
    subprocess.run(cmd,check=True)
    subprocess.run([str(p/'test')],cwd=temp,check=True)
