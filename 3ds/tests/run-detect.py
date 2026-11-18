#!/usr/bin/env python3
"""Builds production detect.c with host stand-ins; sdmc: is a fresh temp dir."""
import pathlib, shutil, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
include = next((p for p in ('/opt/homebrew/include', '/usr/local/include', '/usr/include') if pathlib.Path(p, 'jansson.h').exists()), None)
lib = str(pathlib.Path(include).parent / 'lib') if include else None
with tempfile.TemporaryDirectory(prefix='forge-detect-test-') as temp:
    exe = pathlib.Path(temp) / 'test'
    cmd = [shutil.which('clang') or 'cc', '-std=gnu11', '-g', '-O1', '-Wall', '-Wno-unused-function',
           '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', str(root / 'tests/detect-test.c'), '-ljansson', '-o', str(exe)]
    if include:
        cmd[1:1] = ['-I' + include, '-L' + lib]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe), str(root.parent / 'catalog/forge-store.json'), str(root / 'romfs/universal.json')],
                   cwd=temp, check=True)
