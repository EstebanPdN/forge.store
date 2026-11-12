#!/usr/bin/env python3
"""Verify native update icon coverage and morph continuity under ASan/UBSan."""
from pathlib import Path
import subprocess
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--frames', type=Path, help='Optional local directory for 40×40 PGM frames')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = root / 'build/host-update-icon'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'update-icon-test'
subprocess.run(['clang', '-g', '-O1', '-fsanitize=address,undefined',
                '-I' + str(root / 'source'), str(root / 'tests/update-icon-test.c'),
                '-lm', '-o', str(binary)], check=True)
if args.frames:
    args.frames.mkdir(parents=True, exist_ok=True)
subprocess.run([str(binary), *([str(args.frames)] if args.frames else [])], check=True)
