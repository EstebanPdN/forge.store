#!/usr/bin/env python3
"""Encode the prototype's local videos to FVD2: Theora video + IMA ADPCM audio.

Pipeline, per catalog video (catalog/forge-store.json):
  1. ffmpeg   exact 3DS-resolution master (FFV1, lossless): 20 fps CFR, BT.601 limited
              range 4:2:0, 400x240 (top) / 320x240 (bottom). Pixel clips keep the nearest
              sampling and pixel phase of the MJPEG packer; trailers stay bilinear.
  2. HandBrakeCLI  Theora encode of that master, no scaling and no filters.
              HandBrake fixes the Theora keyframe interval at (frame rate + 0.5) * 10
              frames and accepts no encoder options, so the master is *declared* at
              2 fps (same frames, timestamps x10): keyint becomes 25 frames = 1.25 s at
              the real 20 fps. Bitrates given to HandBrake are therefore real kbps / 10.
  3. ffmpeg   remux MKV -> Ogg (stream copy); the raw Theora packets are read from the Ogg
              pages here. Frames HandBrake left out are zero-byte Theora packets
              (duplicate frames) and are restored as empty packets from the timestamps.
  4. Audio    mono 24000 Hz, 4-bit IMA ADPCM, one block per frame record with the
              predictor / step index stored first, so any record decodes on its own.

FVD2 layout (little endian):
  header, 64 bytes
     0 char[4] "FVD2"       4 u16 version (1)     6 u16 flags (1 bottom, 2 audio)
     8 u32 fps numerator   12 u32 fps denominator 16 u32 frame count
    20 u16 top width, u16 top height, u16 bottom width, u16 bottom height
    28 u32 audio rate      32 u16 audio channels  34 u16 audio codec (0 none, 1 IMA ADPCM)
    36 u32 audio samples per record               40 u32 largest Theora packet (bytes)
    44 u32 setup bytes     48 u32 index offset    52 u32 data offset
    56 u32 file bytes      60 u32 reserved (0)
  setup, at 64: per stream (top, then bottom) the three Theora headers, each u32 length + bytes
  index: frame count x u32 = record offset (bits 0-29) | bottom keyframe (bit 30) | top keyframe (bit 31)
  records: u32 top bytes, u32 bottom bytes, u32 audio bytes, then audio block
           (s16 predictor, u8 step index, u8 0, 4-bit codes low nibble first), top packet,
           bottom packet. A zero-byte packet repeats the previous picture of that screen.

Output goes to 3ds/media-next (never 3ds/media or the romfs); report.json lists every file.
Usage: 3ds/tools/.venv/bin/python -I 3ds/tools/pack-video.py [--only app-id ...] [--force]
"""
from pathlib import Path
import argparse, hashlib, json, re, shutil, struct, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[2]
FPS = 20
DECLARED_FPS = 2                    # what HandBrake is told; its Theora keyint is (rate + 0.5) * 10
AUDIO_RATE = 24000
AUDIO_SPF = AUDIO_RATE // FPS       # 1200 samples in every record
TRAILER_CAP = 6_000_000             # bytes; the encode fills this budget without exceeding it
CLIP_QUALITY = 52                   # HandBrake -q (Theora 0-63, higher is better) for the split pixel clips
CLIP_CAP = 1_200_000                # bytes; a clip over this is re-encoded at a lower quality until it fits
MAX_PACKET = 256 * 1024
TOP, BOTTOM = (400, 240), (320, 240)
HANDBRAKE = shutil.which('HandBrakeCLI') or '/opt/homebrew/bin/HandBrakeCLI'
TAGS = ['-colorspace', 'smpte170m', '-color_primaries', 'smpte170m', '-color_trc', 'smpte170m', '-color_range', 'tv']
YUV = 'out_color_matrix=bt601:out_range=tv'

# ---- IMA ADPCM (the C decoder in video.c mirrors this exactly) ----------------------------------
STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
         118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
         963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
         5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
         24623, 27086, 29794, 32767]
INDEX_STEP = [-1, -1, -1, -1, 2, 4, 6, 8]


def adpcm_encode_block(samples, predictor, index):
    """One record: 4-byte state header + 4-bit codes. Returns (block, predictor, index)."""
    out = bytearray(struct.pack('<hBB', predictor, index, 0))
    byte = 0
    for n, sample in enumerate(samples):
        step = STEPS[index]
        diff = sample - predictor
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        delta = step >> 3
        if diff >= step:
            code |= 4
            diff -= step
            delta += step
        if diff >= step >> 1:
            code |= 2
            diff -= step >> 1
            delta += step >> 1
        if diff >= step >> 2:
            code |= 1
            delta += step >> 2
        predictor = max(-32768, min(32767, predictor - delta if code & 8 else predictor + delta))
        index = max(0, min(88, index + INDEX_STEP[code & 7]))
        if n & 1:
            out.append(byte | code << 4)
        else:
            byte = code
    if len(samples) & 1:
        out.append(byte)
    return bytes(out), predictor, index


def adpcm_decode_block(block):
    """Reference decoder (used by check-media.py and the host test vectors)."""
    predictor, index, _ = struct.unpack_from('<hBB', block)
    out = []
    for byte in block[4:]:
        for code in (byte & 15, byte >> 4):
            step = STEPS[index]
            delta = step >> 3
            if code & 4: delta += step
            if code & 2: delta += step >> 1
            if code & 1: delta += step >> 2
            predictor = max(-32768, min(32767, predictor - delta if code & 8 else predictor + delta))
            index = max(0, min(88, index + INDEX_STEP[code & 7]))
            out.append(predictor)
    return out


# ---- encoding -------------------------------------------------------------------------------------
def run(cmd, log=None):
    result = subprocess.run([str(c) for c in cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if log: Path(log).write_bytes(result.stdout)
    if result.returncode:
        raise RuntimeError(f"{cmd[0]} failed ({result.returncode}):\n{result.stdout.decode(errors='replace')[-2000:]}")
    return result.stdout


def master(src, dest, size, pixel):
    """Lossless 3DS-resolution master, declared at DECLARED_FPS (see the module docstring)."""
    w, h = size
    if pixel:
        probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                                                    'stream=width,height', '-of', 'json', str(ROOT / src)]))['streams'][0]
        source = (probe['width'], probe['height'])
        if source == (w * 2, h * 2):
            # Chromium image-rendering:pixelated selects the upper/left sample when halving
            # these 2x split-screen sources; swscale selects lower/right. Shift in RGB to keep
            # that pixel phase, pick pixels with nearest sampling, and only then go to 4:2:0:
            # luma is exact per pixel, chroma is the centred 2x2 average Theora expects.
            chain = (f'format=rgb24,pad=iw+2:ih+2:1:1,crop=iw-2:ih-2:0:0,scale={w}:{h}:flags=neighbor,format=rgb24,'
                     f'scale={w}:{h}:flags=area:{YUV}:out_h_chr_pos=128:out_v_chr_pos=128')
        elif source == (w, h):
            chain = f'scale={w}:{h}:flags=neighbor:{YUV}'
        else:
            raise ValueError(f'Pixel-video sampling needs a reviewed 1x/2x source: {src}')
    else:
        chain = f'scale={w}:{h}:force_original_aspect_ratio=decrease:flags=bilinear:{YUV},pad={w}:{h}:(ow-iw)/2:(oh-ih)/2'
    run(['ffmpeg', '-v', 'error', '-threads', '2', '-y', '-i', ROOT / src, '-an',
         '-vf', f'fps={FPS},{chain},format=yuv420p,setpts=N/({DECLARED_FPS}*TB)', '-r', DECLARED_FPS,
         *TAGS, '-c:v', 'ffv1', '-level', '3', '-g', '1', dest])
    return int(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-count_packets',
                                        '-show_entries', 'stream=nb_read_packets', '-of', 'csv=p=0', str(dest)]).split()[0])


def handbrake(src, dest, size, rate):
    """Theora encode with HandBrakeCLI; rate is ('bitrate', kbps at the declared rate) or ('quality', q)."""
    mode = ['-b', rate[1], '--multi-pass', '--no-turbo'] if rate[0] == 'bitrate' else ['-q', rate[1]]
    cmd = [HANDBRAKE, '-i', src, '-o', dest, '-f', 'av_mkv', '-e', 'theora', *mode, '--rate', DECLARED_FPS, '--cfr',
           '--width', size[0], '--height', size[1], '--crop', '0:0:0:0', '--non-anamorphic', '-a', 'none',
           '--no-comb-detect', '--no-decomb', '--no-deinterlace', '--no-detelecine', '--no-hqdn3d', '--no-nlmeans',
           '--no-deblock', '--no-chroma-smooth', '--no-unsharp', '--no-lapsharp', '--no-grayscale', '--no-markers']
    log = run(cmd, Path(dest).with_suffix('.log')).decode(errors='replace')
    keyint = re.search(r'theora: keyint: (\d+)', log)
    if not keyint or int(keyint.group(1)) > 2 * FPS:
        raise RuntimeError(f'HandBrake Theora keyframe interval is not short: {keyint and keyint.group(1)}')
    return cmd


def ogg_packets(data):
    """All packets of a single-stream Ogg file, in order."""
    packets, current, pos = [], b'', 0
    while pos < len(data):
        if data[pos:pos + 4] != b'OggS' or data[pos + 4]:
            raise ValueError('Invalid Ogg page')
        segments = data[pos + 26]
        table = data[pos + 27:pos + 27 + segments]
        pos += 27 + segments
        for length in table:
            current += data[pos:pos + length]
            pos += length
            if length < 255:
                packets.append(current)
                current = b''
    if current: raise ValueError('Truncated Ogg packet')
    return packets


def theora_stream(mkv, frames, size):
    """(three headers, one packet per frame). Missing timestamps are zero-byte duplicate frames."""
    packets = ogg_packets(subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(mkv), '-map', '0:v:0', '-c', 'copy', '-f', 'ogg', '-']))
    headers, data = packets[:3], packets[3:]
    if [p[:7] for p in headers] != [bytes([0x80 + i]) + b'theora' for i in range(3)]:
        raise ValueError('Missing Theora headers')
    width, height = struct.unpack_from('>HH', headers[0], 10)
    if (width * 16, height * 16) != size or headers[0][7] != 3:
        raise ValueError(f'Unexpected Theora frame size {width * 16}x{height * 16}')
    times = subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'packet=pts_time',
                                     '-of', 'csv=p=0', str(mkv)]).split()
    slots = [round(float(t) * DECLARED_FPS) for t in times]
    if len(slots) != len(data) or slots != sorted(set(slots)) or slots[0] != 0 or slots[-1] >= frames:
        raise ValueError('Theora packets do not map to frames')
    out = [b''] * frames
    for slot, packet in zip(slots, data):
        if not packet or packet[0] & 0x80 or len(packet) > MAX_PACKET:
            raise ValueError('Unexpected Theora data packet')
        out[slot] = packet
    if out[0][0] & 0x40: raise ValueError('First Theora frame is not a keyframe')
    return headers, out


def is_key(packet): return bool(packet) and not packet[0] & 0x40


def quality(mkv, reference):
    """Luma/overall PSNR of the encode against its lossless master (dB)."""
    log = subprocess.run(['ffmpeg', '-hide_banner', '-i', str(mkv), '-i', str(reference), '-lavfi',
                          f'[0:v]fps={DECLARED_FPS}[a];[a][1:v]psnr', '-f', 'null', '-'], capture_output=True, text=True).stderr
    match = re.search(r'PSNR y:([\d.]+) .*average:([\d.]+) min:([\d.]+)', log)
    return {'psnr_y': float(match.group(1)), 'psnr_avg': float(match.group(2)), 'psnr_min': float(match.group(3))} if match else {}


def write_fvd2(path, top, bottom, audio_blocks):
    """top/bottom: (headers, packets); bottom may be None. audio_blocks: one per frame, or []."""
    frames = len(top[1])
    setup = b''.join(struct.pack('<I', len(h)) + h for stream in (top, bottom) if stream for h in stream[0])
    index_offset = 64 + len(setup)
    data_offset = index_offset + 4 * frames
    index, body, offset = [], bytearray(), data_offset
    for i in range(frames):
        a = top[1][i]
        b = bottom[1][i] if bottom else b''
        pcm = audio_blocks[i] if audio_blocks else b''
        if offset >= 1 << 30: raise ValueError('FVD2 is limited to 1 GiB')
        index.append(offset | is_key(a) << 31 | is_key(b) << 30)
        record = struct.pack('<III', len(a), len(b), len(pcm)) + pcm + a + b
        body += record
        offset += len(record)
    largest = max(len(p) for stream in (top, bottom) if stream for p in stream[1])
    header = struct.pack('<4sHHIIIHHHHIHHIIIIIII', b'FVD2', 1, (1 if bottom else 0) | (2 if audio_blocks else 0), FPS, 1, frames,
                         *TOP, *(BOTTOM if bottom else (0, 0)), AUDIO_RATE if audio_blocks else 0, 1 if audio_blocks else 0,
                         1 if audio_blocks else 0, AUDIO_SPF if audio_blocks else 0, largest, len(setup), index_offset,
                         data_offset, offset, 0)
    assert len(header) == 64
    staging = path.with_suffix('.fvid.tmp')
    staging.write_bytes(header + setup + struct.pack(f'<{frames}I', *index) + body)
    staging.replace(path)
    return offset


def key_stats(packets):
    keys = [i for i, p in enumerate(packets) if is_key(p)]
    gaps = [b - a for a, b in zip(keys, keys[1:] + [len(packets)])]
    return {'keyframes': len(keys), 'max_interval_frames': max(gaps), 'max_interval_seconds': max(gaps) / FPS,
            'mean_interval_seconds': round(len(packets) / len(keys) / FPS, 3), 'duplicate_frames': sum(1 for p in packets if not p)}


def encode_video(app, video, out, work):
    name = f"{app['id']}-{video['id']}"
    split = bool(video.get('top'))
    top_master, bottom_master = work / f'{name}-top-master.mkv', work / f'{name}-bottom-master.mkv'
    frames = master(video.get('src', video.get('top')), top_master, TOP, split)
    commands, audio_blocks, bottom, bottom_quality = [], [], None, None
    if video.get('audio'):
        pcm = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(ROOT / video['src']), '-vn', '-ac', '1', '-ar', str(AUDIO_RATE), '-f', 's16le', '-'])
        samples = list(struct.unpack(f'<{len(pcm) // 2}h', pcm[:len(pcm) // 2 * 2]))
        samples = (samples + [0] * (frames * AUDIO_SPF))[:frames * AUDIO_SPF]
        predictor = index = 0
        for i in range(frames):
            block, predictor, index = adpcm_encode_block(samples[i * AUDIO_SPF:(i + 1) * AUDIO_SPF], predictor, index)
            audio_blocks.append(block)
    top_mkv = work / f'{name}-top.mkv'
    if split:
        bottom_frames = master(video['bottom'], bottom_master, BOTTOM, True)
        # Constant quality, both screens alike. Size grows with q, so when the default does
        # not fit under the cap, bisect for the highest q that does.
        tried = {}
        def attempt(q):
            top_mkv, bottom_mkv = work / f'{name}-top-q{q}.mkv', work / f'{name}-bottom-q{q}.mkv'
            cmds = [handbrake(top_master, top_mkv, TOP, ('quality', q)), handbrake(bottom_master, bottom_mkv, BOTTOM, ('quality', q))]
            top = theora_stream(top_mkv, frames, TOP)
            headers, packets = theora_stream(bottom_mkv, bottom_frames, BOTTOM)
            # As before, the top screen sets the length; a shorter bottom stream holds its last picture.
            bottom = (headers, (packets + [b''] * frames)[:frames])
            tried[q] = (write_fvd2(out, top, bottom, audio_blocks), top, bottom, cmds, top_mkv, bottom_mkv)
            return tried[q][0] <= CLIP_CAP
        q = CLIP_QUALITY
        if not attempt(q):
            low, high = 0, q            # invariant: low fits (or is the floor), high does not
            while high - low > 1:
                q = (low + high) // 2
                low, high = (q, high) if attempt(q) else (low, q)
            q = low
            if q not in tried and not attempt(q): raise RuntimeError(f'{name} does not fit in {CLIP_CAP} bytes')
        _, top, bottom, cmds, top_mkv, bottom_mkv = tried[q]
        size = write_fvd2(out, top, bottom, audio_blocks)
        commands += cmds
        bottom_quality = quality(bottom_mkv, bottom_master)
        rate = {'mode': 'constant quality', 'handbrake_q': q, 'default_q': CLIP_QUALITY, 'cap_bytes': CLIP_CAP,
                'attempts': {str(k): v[0] for k, v in sorted(tried.items())}}
    else:
        # Two-pass average bitrate: take the largest whole kbps (at the declared rate) whose
        # finished file still fits under the cap. Usually the first or second attempt.
        fixed = 64 + 4 * frames + 12 * frames + sum(map(len, audio_blocks)) + 4096
        per_kbps = frames / DECLARED_FPS * 1000 / 8      # bytes one more declared kbps costs
        kbps = int((TRAILER_CAP - fixed) / per_kbps)
        tried = {}
        while kbps > 0 and kbps not in tried:
            mkv = work / f'{name}-top-{kbps}.mkv'
            cmd = handbrake(top_master, mkv, TOP, ('bitrate', kbps))
            stream = theora_stream(mkv, frames, TOP)
            size = write_fvd2(out, stream, None, audio_blocks)
            tried[kbps] = (size, stream, cmd, mkv)
            if size > TRAILER_CAP: kbps -= 1
            elif size + per_kbps <= TRAILER_CAP: kbps += 1
        fitting = [k for k, v in tried.items() if v[0] <= TRAILER_CAP]
        if not fitting: raise RuntimeError(f'{name} does not fit in {TRAILER_CAP} bytes')
        kbps = max(fitting)
        _, top, cmd, top_mkv = tried[kbps]
        size = write_fvd2(out, top, None, audio_blocks)
        commands.append(cmd)
        rate = {'mode': 'two-pass average bitrate', 'handbrake_kbps_at_declared_rate': kbps,
                'target_video_kbps': kbps * FPS // DECLARED_FPS, 'cap_bytes': TRAILER_CAP,
                'attempts': {str(k): v[0] for k, v in tried.items()}}
    data = out.read_bytes()
    duration = frames / FPS
    video_bytes = sum(len(p) for stream in (top, bottom) if stream for p in stream[1])
    report = {'file': out.name, 'key': f"{app['id']}/{video['id']}", 'bytes': size, 'sha256': hashlib.sha256(data).hexdigest(),
              'frames': frames, 'fps': FPS, 'duration': duration, 'bitrate_kbps': round(size * 8 / duration / 1000, 1),
              'video_kbps': round(video_bytes * 8 / duration / 1000, 1),
              'audio_kbps': round(sum(map(len, audio_blocks)) * 8 / duration / 1000, 1),
              'audio': {'codec': 'IMA ADPCM 4-bit', 'rate': AUDIO_RATE, 'channels': 1} if audio_blocks else None,
              'rate_control': rate, 'top': {**key_stats(top[1]), **quality(top_mkv, top_master)},
              'bottom': {**key_stats(bottom[1]), **bottom_quality} if bottom else None,
              'handbrake': [' '.join(str(c) for c in cmd) for cmd in commands]}
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=Path, default=ROOT / '3ds/media-next')
    ap.add_argument('--only', nargs='*', help='file stems to encode, e.g. alttp-trailer')
    ap.add_argument('--force', action='store_true', help='re-encode even when the output is newer than its sources')
    ap.add_argument('--work', type=Path, help='keep masters, HandBrake outputs and logs in this directory')
    a = ap.parse_args()
    for forbidden in (ROOT / '3ds/media', ROOT / '3ds/romfs', ROOT / 'assets/store-resources'):
        if a.out.resolve() == forbidden or forbidden in a.out.resolve().parents:
            raise SystemExit(f'Refusing to write into {forbidden}: review and publish from 3ds/media-next instead')
    a.out.mkdir(parents=True, exist_ok=True)
    report_path = a.out / 'report.json'
    previous = {r['file']: r for r in json.loads(report_path.read_text())['files']} if report_path.exists() else {}
    reports = []
    for app in json.loads((ROOT / 'catalog/forge-store.json').read_text())['apps']:
        for video in app.get('videos', []):
            name = f"{app['id']}-{video['id']}"
            out = a.out / (name + '.fvid')
            sources = [ROOT / video[k] for k in ('src', 'top', 'bottom') if video.get(k)]
            fresh = out.exists() and out.name in previous and out.stat().st_mtime > max(p.stat().st_mtime for p in sources + [Path(__file__)])
            if (a.only and name not in a.only) or (fresh and not a.force):
                if out.name in previous: reports.append(previous[out.name])
                continue
            with tempfile.TemporaryDirectory(prefix='forge-video-') as temp:
                work = a.work or Path(temp)
                work.mkdir(parents=True, exist_ok=True)
                report = encode_video(app, video, out, work)
            reports.append(report)
            print(f"{name}: {report['frames']} frames, {report['bytes'] / 1e6:.2f} MB, video {report['video_kbps']} kbps, "
                  f"keyframes <= {report['top']['max_interval_seconds']} s, PSNR-Y {report['top'].get('psnr_y')} dB", flush=True)
    versions = {'handbrake': subprocess.run([HANDBRAKE, '--version'], capture_output=True, text=True).stdout.strip().splitlines()[-1],
                'ffmpeg': subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0]}
    report_path.write_text(json.dumps({'format': 'FVD2', 'tools': versions, 'total_bytes': sum(r['bytes'] for r in reports), 'files': reports}, indent=2) + '\n')
    print(f"{len(reports)} files, {sum(r['bytes'] for r in reports) / 1e6:.2f} MB total -> {a.out}")


if __name__ == '__main__':
    main()
