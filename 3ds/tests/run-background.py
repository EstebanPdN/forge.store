#!/usr/bin/env python3
"""Production background.c: no gsp::Lcd session survives a frame with the lid open (HOME freeze)."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='forge-background-') as temp:
    exe = Path(temp) / 'background-test'
    subprocess.run(['clang', '-std=gnu11', '-g', '-O1', '-Wall', '-Wno-unused-function', '-fsanitize=address,undefined',
                    '-I' + str(root / 'source'), str(root / 'tests/background-test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: background lid/LED handling never keeps gsp::Lcd open while the lid is open')
