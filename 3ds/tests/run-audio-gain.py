#!/usr/bin/env python3
"""Measure the production PCM gain on every bundled music, intro and SFX file."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/host-audio-gain'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'audio-gain-test'
subprocess.run(['clang', '-g', '-O1', '-fsanitize=address,undefined',
                '-I' + str(root / 'source'), str(root / 'tests/audio-gain-test.c'),
                '-lm', '-o', str(binary)], check=True)
assets = sorted((root / 'romfs/audio').glob('*.pcm'))
assert assets, 'Missing bundled PCM fixtures'
subprocess.run([str(binary), *map(str, assets)], check=True)
print(f'PASS: {len(assets)} PCM assets and all 65,536 signed sample inputs; ASan/UBSan.')
