#!/usr/bin/env python3
"""Run the website progress parser under ASan/UBSan; optionally against a saved page."""
from pathlib import Path
import subprocess, sys
root = Path(__file__).resolve().parents[1]
out = root / 'build/host-site-progress'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'site-progress-test'
subprocess.run(['clang', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I' + str(root / 'source'), str(root / 'tests/site-progress-test.c'), '-o', str(binary)], check=True)
subprocess.run([str(binary), *sys.argv[1:2]], check=True)
