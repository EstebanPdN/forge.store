#!/usr/bin/env python3
"""Builds production rom_find.c against real files and the shipped catalog's rules, and the SD tree (install_plan.c)."""
import pathlib, shutil, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
include = next((p for p in ('/opt/homebrew/include', '/usr/local/include', '/usr/include') if pathlib.Path(p, 'jansson.h').exists()), None)
lib = str(pathlib.Path(include).parent / 'lib') if include else None
with tempfile.TemporaryDirectory(prefix='forge-rom-find-test-') as temp:
    exe = pathlib.Path(temp) / 'test'
    cmd = [shutil.which('clang') or 'cc', '-std=gnu11', '-g', '-O1', '-Wall', '-Werror', '-DROM_FIND_HOST_TEST',
           '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
           str(root / 'tests/rom-find-test.c'), str(root / 'source/rom_find.c'), '-ljansson', '-lz', '-o', str(exe)]
    if include:
        cmd[1:1] = ['-I' + include, '-L' + lib]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe), str(root.parent / 'catalog/forge-store.json')], cwd=temp, check=True)
    plan = pathlib.Path(temp) / 'plan'
    cmd = [shutil.which('clang') or 'cc', '-std=gnu11', '-g', '-Wall', '-Werror', '-fsanitize=address,undefined',
           str(root / 'tests/markup/plan_test.c'), str(root / 'source/install_plan.c'), '-ljansson', '-o', str(plan)]
    if include:
        cmd[1:1] = ['-I' + include, '-L' + lib]
    subprocess.run(cmd, check=True)
    subprocess.run([str(plan)], check=True)
