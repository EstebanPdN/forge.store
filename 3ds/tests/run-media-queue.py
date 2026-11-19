#!/usr/bin/env python3
"""Production media.c (with assets.c): previews and resource packs through one worker.

Transport, SHA-256 and SD space are host shims (media_host_platform.h). The registry is
loaded once per process, so the pack scenarios run in their own process."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='forge-media-queue-') as temp:
    t = Path(temp)
    common = ['-std=gnu11', '-g', '-O1', '-Wno-deprecated-declarations', '-I/opt/homebrew/include',
              str(root / 'tests/media_queue_test.c'), '-DASSETS_HOST_TEST', str(root / 'source/assets.c'),
              '-L/opt/homebrew/lib', '-ljansson', '-lz', '-lpthread']
    for name, flags in (('asan', ['-fsanitize=address,undefined']), ('tsan', ['-fsanitize=thread'])):
        exe = t / name
        subprocess.run(['clang', *flags, *common, '-o', str(exe)], check=True)
        for mode in ([], ['packs']):
            out = subprocess.run([str(exe), *mode], check=True, capture_output=True, text=True, timeout=300).stdout
            print(f'{name} {mode[0] if mode else "previews"}: {out.strip().splitlines()[-1]}')
print('PASS: media queue with resource packs (ASan/UBSan, TSan)')
