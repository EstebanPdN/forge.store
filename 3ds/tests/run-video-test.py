#!/usr/bin/env python3
"""Builds and runs tests/video-test.c: the production video.c on pthreads with host
libtheoradec/libjpeg-turbo (brew install theora jpeg-turbo), under ASan+UBSan and TSan.

Fixtures are made here from the encoded previews in 3ds/media-next:
  clip.fvid / clip.ref        doom-forest (two screens); every frame decoded by ffmpeg's own
                              Theora decoder and converted with the decoder's integer formula
  trailer.fvid / .ref / .pcm  alttp-trailer; reference pictures for the first 220 frames and
                              around each seek target, the whole sound track decoded by the
                              Python ADPCM reference
  trailer.seeks               seek targets chosen from the real keyframe / duplicate layout
  adpcm.bin                   ADPCM blocks (encoder output and arbitrary codes) + expected PCM
  legacy.fvid / legacy.ref    the published MJPEG doom-forest, when 3ds/media still has it

Usage: 3ds/tools/.venv/bin/python -I 3ds/tests/run-video-test.py [--all] [--video-c PATH] [--media DIR] [--keep DIR]
"""
from pathlib import Path
import argparse, io, random, runpy, shutil, struct, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[1]
check = runpy.run_path(str(ROOT / 'tools/check-media.py'))
pack = runpy.run_path(str(ROOT / 'tools/pack-video.py'))


def references(info, wanted):
    """{frame index: [rgb565 bytes per screen]} for the wanted frames."""
    import numpy as np
    out = {i: [] for i in wanted}
    for stream in info['streams']:
        raw, size = check['decode_yuv'](stream)
        pictures = check['frame_map'](stream)
        for i in wanted:
            n = pictures[i]
            out[i].append(check['rgb565'](raw[n * size:(n + 1) * size], stream['width']).astype('<u2').tobytes())
    return out


def write_ref(path, frames, bottom):
    with open(path, 'wb') as f:
        f.write(struct.pack('<II', len(frames), bottom))
        for index in sorted(frames):
            f.write(struct.pack('<I', index) + b''.join(frames[index]))


def fixtures(out, media):
    clip = (media / 'doom-forest.fvid').read_bytes()
    (out / 'clip.fvid').write_bytes(clip)
    info = check['parse_fvd2'](clip)
    write_ref(out / 'clip.ref', references(info, range(info['frames'])), 1)

    trailer = (media / 'alttp-trailer.fvid').read_bytes()
    (out / 'trailer.fvid').write_bytes(trailer)
    info = check['parse_fvd2'](trailer)
    packets, count = info['streams'][0]['packets'], info['frames']
    keys = [i for i, p in enumerate(packets) if p and not p[0] & 0x40]
    runs = [i for i in range(300, count - 8) if not packets[i] and not packets[i + 1] and not packets[i + 2] and packets[i - 1]]
    assert len(keys) > 60 and runs, 'the trailer no longer has the keyframe / duplicate layout these targets rely on'
    after_run = next(i for i in range(runs[0], count) if packets[i])
    seeks = [keys[5], keys[5] - 1, keys[5] + 1, keys[9] + 12, runs[0] + 1, after_run, 1, keys[40] + 24, keys[20], keys[60] - 2,
             keys[3] + 7, keys[-1], 2000, count - 3]
    (out / 'trailer.seeks').write_bytes(struct.pack(f'<{len(seeks)}I', *seeks))
    wanted = set(range(220)) | {i for s in seeks + [0] for i in range(s, min(s + 8, count))}
    write_ref(out / 'trailer.ref', references(info, sorted(wanted)), 0)
    pcm = []
    for block in info['blocks']:
        pcm += pack['adpcm_decode_block'](block)[:info['samples']]
    (out / 'trailer.pcm').write_bytes(struct.pack(f'<{len(pcm)}h', *pcm))

    rng = random.Random(20261110)
    vectors = bytearray()
    signals = [[int(20000 * ((i * 37 % 200) / 100 - 1)) for i in range(1200)],        # sawtooth
               [rng.randint(-32768, 32767) for _ in range(1200)],                     # full-scale noise
               [32767 if i // 7 % 2 else -32768 for i in range(1201)],                # clipping square, odd length
               [0] * 64, [rng.randint(-200, 200) for _ in range(2401)]]
    for signal in signals:
        for predictor, index in ((0, 0), (-32768, 88), (32767, 40)):
            block, _, _ = pack['adpcm_encode_block'](signal, predictor, index)
            expected = pack['adpcm_decode_block'](block)[:len(signal)]
            vectors += struct.pack('<II', len(block), len(expected)) + block + struct.pack(f'<{len(expected)}h', *expected)
    for _ in range(40):  # arbitrary codes: every clamp of predictor and step index
        block = struct.pack('<hBB', rng.randint(-32768, 32767), rng.randint(0, 88), 0) + bytes(rng.choice((0x77, 0xff, 0x00, rng.randint(0, 255))) for _ in range(rng.randint(1, 600)))
        expected = pack['adpcm_decode_block'](block)
        vectors += struct.pack('<II', len(block), len(expected)) + block + struct.pack(f'<{len(expected)}h', *expected)
    (out / 'adpcm.bin').write_bytes(vectors)

    legacy = ROOT / 'media/doom-forest.fvid'
    if legacy.exists() and legacy.read_bytes()[:4] == b'FVID':
        import numpy as np
        from PIL import Image
        data = legacy.read_bytes()
        (out / 'legacy.fvid').write_bytes(data)
        count, bottom = struct.unpack_from('<II', data, 8)
        offsets = struct.unpack_from(f'<{count}I', data, 20)
        frames = {}
        for i in range(81):
            top_n, bot_n, _ = struct.unpack_from('<III', data, offsets[i])
            frames[i] = []
            for start, length in ((offsets[i] + 12, top_n), (offsets[i] + 12 + top_n, bot_n)):
                if not length: continue
                rgb = np.asarray(Image.open(io.BytesIO(data[start:start + length])).convert('RGB')).astype(np.uint16)
                frames[i].append(((rgb[..., 0] >> 3) << 11 | (rgb[..., 1] >> 2) << 5 | rgb[..., 2] >> 3).astype('<u2').tobytes())
        write_ref(out / 'legacy.ref', frames, bottom)


def fingerprints(out, path):
    """<stem>.hashes: a weighted sum of every frame's RGB565 words, per screen (see video-test.c)."""
    import numpy as np
    info = check['parse_fvd2'](path.read_bytes())
    columns = []
    for stream in info['streams']:
        raw, size = check['decode_yuv'](stream)
        weights = (np.arange(stream['width'] * 240, dtype=np.uint64) * np.uint64(0x9E3779B97F4A7C15) + np.uint64(0x1234567)) | np.uint64(1)
        coded = []
        for start in range(0, len(raw) // size, 64):
            pixels = check['rgb565'](raw[start * size:(start + 64) * size], stream['width']).astype(np.uint64).reshape(-1, stream['width'] * 240)
            coded += list((pixels * weights).sum(axis=1, dtype=np.uint64))
        columns.append([coded[n] for n in check['frame_map'](stream)])
    with open(out / (path.stem + '.hashes'), 'wb') as f:
        f.write(struct.pack('<II', info['frames'], len(columns)))
        for row in zip(*columns):
            f.write(struct.pack(f'<{len(row)}Q', *(int(v) for v in row)))
    shutil.copy(path, out / path.name)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--video-c', type=Path, default=ROOT / 'source/video.c', help='decoder source under test')
    ap.add_argument('--media', type=Path, default=ROOT.parent / 'assets/store-resources')
    ap.add_argument('--keep', type=Path, help='keep fixtures and binaries here')
    ap.add_argument('--sanitizers', nargs='*', default=['address,undefined', 'thread'])
    ap.add_argument('--all', action='store_true', help='also play every *.fvid of --media from start to end, checking every picture')
    a = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='forge-video-test-') as temp:
        out = a.keep or Path(temp)
        out.mkdir(parents=True, exist_ok=True)
        fixtures(out, a.media)
        for sanitizer in a.sanitizers:
            exe = out / ('video-test-' + sanitizer.split(',')[0])
            subprocess.run(['clang', '-std=gnu11', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
                            '-Wno-missing-field-initializers', '-fsanitize=' + sanitizer, '-fno-omit-frame-pointer',
                            f'-DVIDEO_C="{a.video_c.resolve()}"', '-I', str(ROOT / 'source'), '-I/opt/homebrew/include',
                            str(ROOT / 'tests/video-test.c'), '-L/opt/homebrew/lib', '-ltheoradec', '-logg', '-ljpeg', '-lpthread', '-lm',
                            '-o', str(exe)], check=True)
            print(f'--- {sanitizer}', flush=True)
            result = subprocess.run([str(exe), str(out)])
            if result.returncode: sys.exit(f'video-test failed under -fsanitize={sanitizer}')
        if a.all:
            exe = out / ('video-test-' + a.sanitizers[0].split(',')[0])
            print('--- every encoded preview, start to end', flush=True)
            for path in sorted(a.media.glob('*.fvid')):
                fingerprints(out, path)
                if subprocess.run([str(exe), str(out), 'play', path.stem]).returncode: sys.exit(f'{path.name} failed')
    print('PASS: video-test under', ' and '.join(a.sanitizers))


if __name__ == '__main__':
    main()
