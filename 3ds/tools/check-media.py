#!/usr/bin/env python3
"""Validate encoded previews: FVD2 (Theora + IMA ADPCM) and legacy FVID (MJPEG + PCM).

  check-media.py                 files named by 3ds/romfs/media.json, read from assets/store-resources
                                 (hash and size must match the manifest); writes
                                 3ds/build/gen/media-check.json as before
  check-media.py --dir DIR       every *.fvid in DIR (hashes against DIR/report.json when
                                 present); writes nothing unless --json PATH is given

FVD2: header, setup headers, the whole index and every record boundary, keyframe flags
against the packets, ADPCM block state continuity, and a full ffmpeg decode of each Theora
stream (rebuilt as Ogg) to prove every packet decodes at the declared size.
FVID: tables, every chunk boundary and sampled JPEG decodes.
"""
from pathlib import Path
import argparse, hashlib, io, json, runpy, struct, subprocess, zlib

ROOT = Path(__file__).resolve().parents[2]
OFFSET_MASK, KEY_TOP, KEY_BOTTOM = 0x3fffffff, 0x80000000, 0x40000000
REVERSE = bytes(int(f'{i:08b}'[::-1], 2) for i in range(256))


def parse_fvd2(data):
    """Strict parse. Returns a dict with header fields, per-stream headers/packets and audio blocks."""
    (magic, version, flags, fps, fps_den, frames, top_w, top_h, bot_w, bot_h, rate, channels, codec, spf, largest,
     setup_bytes, index_offset, data_offset, file_bytes, reserved) = struct.unpack_from('<4sHHIIIHHHHIHHIIIIIII', data)
    bottom, audio = bool(flags & 1), bool(flags & 2)
    assert magic == b'FVD2' and version == 1 and flags <= 3 and fps_den == 1 and 1 <= fps <= 60 and 0 < frames <= 100000
    assert (top_w, top_h) == (400, 240) and (bot_w, bot_h) == ((320, 240) if bottom else (0, 0))
    assert (8000 <= rate <= 48000 and channels == 1 and codec == 1 and 0 < spf <= 4096) if audio else not (rate or channels or codec or spf)
    assert 0 < largest <= 256 * 1024 and index_offset == 64 + setup_bytes and data_offset == index_offset + 4 * frames
    assert file_bytes == len(data) and not reserved
    pos, streams = 64, []
    for width in (400, 320)[:2 if bottom else 1]:
        headers = []
        for i in range(3):
            n, = struct.unpack_from('<I', data, pos)
            assert 0 < n <= 64 * 1024 and data[pos + 4:pos + 11] == bytes([0x80 + i]) + b'theora'
            headers.append(data[pos + 4:pos + 4 + n])
            pos += 4 + n
        mb_w, mb_h = struct.unpack_from('>HH', headers[0], 10)
        assert (mb_w * 16, mb_h * 16) == (width, 240), (mb_w, mb_h)
        streams.append({'width': width, 'headers': headers, 'packets': []})
    assert pos == index_offset
    index = struct.unpack_from(f'<{frames}I', data, index_offset)
    assert index[0] & OFFSET_MASK == data_offset and index[0] & KEY_TOP and (not bottom or index[0] & KEY_BOTTOM)
    audio_bytes = 4 + (spf + 1) // 2 if audio else 0
    blocks = []
    for i, entry in enumerate(index):
        offset = entry & OFFSET_MASK
        end = index[i + 1] & OFFSET_MASK if i + 1 < frames else len(data)
        lengths = struct.unpack_from('<III', data, offset)
        assert 12 + sum(lengths) == end - offset, (i, lengths, end - offset)
        assert lengths[0] <= largest and lengths[1] <= largest and (bottom or not lengths[1])
        assert lengths[2] == audio_bytes, (i, lengths[2])
        pos = offset + 12
        blocks.append(data[pos:pos + lengths[2]])
        pos += lengths[2]
        for s, flag in enumerate((KEY_TOP, KEY_BOTTOM)[:len(streams)]):
            packet = data[pos:pos + lengths[s]]
            pos += lengths[s]
            assert not packet or not packet[0] & 0x80, (i, s)
            assert bool(entry & flag) == (bool(packet) and not packet[0] & 0x40), ('keyframe flag', i, s)
            streams[s]['packets'].append(packet)
    return {'fps': fps, 'frames': frames, 'bottom': bottom, 'audio': audio, 'rate': rate, 'samples': spf, 'streams': streams,
            'blocks': blocks if audio else [], 'index': index}


def ogg_crc(page):
    """Ogg's CRC-32 (polynomial 0x04c11db7, not reflected, no inversion) via zlib's reflected one."""
    value = zlib.crc32(page.translate(REVERSE), 0xffffffff) ^ 0xffffffff
    return int(f'{value:032b}'[::-1], 2)


def theora_ogg(stream):
    """The stream's coded packets as an Ogg Theora file (one packet per page).
    Duplicate (zero-byte) frames are left out: frame_map() gives the picture of every frame."""
    shift = (struct.unpack_from('>H', stream['headers'][0], 40)[0] >> 5) & 31
    coded = [p for p in stream['packets'] if p]
    out, key = bytearray(), 0
    for sequence, packet in enumerate(stream['headers'] + coded):
        n = sequence - 3
        if n >= 0 and not packet[0] & 0x40: key = n
        granule = 0 if n < 0 else (key + 1) << shift | (n - key)
        assert len(packet) < 255 * 255
        table = bytes([255] * (len(packet) // 255) + [len(packet) % 255])
        kind = 2 if sequence == 0 else 4 if sequence == len(coded) + 2 else 0
        page = struct.pack('<4sBBqIIIB', b'OggS', 0, kind, granule, 0x46564432, sequence, 0, len(table)) + table + packet
        out += page[:22] + struct.pack('<I', ogg_crc(page)) + page[26:]
    return bytes(out)


def frame_map(stream):
    """Index into the coded pictures for every frame (a duplicate frame repeats the previous one)."""
    out, n = [], -1
    for packet in stream['packets']:
        if packet: n += 1
        out.append(n)
    return out


def decode_yuv(stream):
    """Raw 4:2:0 planes of every coded picture, decoded by ffmpeg's own Theora decoder."""
    width = stream['width']
    # One thread: ffmpeg's frame-threaded Theora decode is not reproducible (a few pictures
    # differ from run to run); single-threaded it is exact and matches libtheora.
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-threads', '1', '-f', 'ogg', '-i', '-', '-fps_mode', 'passthrough', '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-'],
                         input=theora_ogg(stream), capture_output=True, check=True).stdout
    size = width * 240 * 3 // 2
    coded = sum(1 for p in stream['packets'] if p)
    assert len(raw) == coded * size, (len(raw) / size, coded)
    return raw, size


def rgb565(raw, width):
    """The decoder's exact integer BT.601 -> RGB565 conversion (video.c convert_tiles), numpy.
    Input: concatenated yuv420p frames. Output: uint16 array [frames, 240, width]."""
    import numpy as np
    frames = np.frombuffer(raw, dtype=np.uint8).reshape(-1, width * 240 * 3 // 2)
    n = frames.shape[0]
    y = frames[:, :width * 240].reshape(n, 240, width).astype(np.int32)
    u = frames[:, width * 240:width * 300].reshape(n, 120, width // 2).astype(np.int32).repeat(2, 1).repeat(2, 2) - 128
    v = frames[:, width * 300:].reshape(n, 120, width // 2).astype(np.int32).repeat(2, 1).repeat(2, 2) - 128
    lut = np.clip(((np.arange(-232 - 16, 256 + 232 - 16) * 76309 + 32768) >> 16), 0, 255).astype(np.uint16)
    r = lut[y + ((v * 89831 + 32768) >> 16) + 232]
    g = lut[y - ((u * 22050 + 32768) >> 16) - ((v * 45757 + 32768) >> 16) + 232]
    b = lut[y + ((u * 113538 + 32768) >> 16) + 232]
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def check_fvd2(data, key):
    info = parse_fvd2(data)
    adpcm = runpy.run_path(str(Path(__file__).with_name('pack-video.py')))
    predictor = index = 0
    for i, block in enumerate(info['blocks']):
        # Each block restates the state the previous one ended in: seeking never changes the sound.
        assert struct.unpack_from('<hBB', block) == (predictor, index, 0), (key, 'ADPCM state', i)
        samples = adpcm['adpcm_decode_block'](block)[:info['samples']]
        predictor = samples[-1]
        index = _final_index(block, adpcm)
    report = {'id': key, 'format': 'FVD2', 'frames': info['frames'], 'duration': info['frames'] / info['fps'], 'bottom': info['bottom'],
              'audio': info['audio'], 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'streams': []}
    for stream, flag in zip(info['streams'], (KEY_TOP, KEY_BOTTOM)):
        raw, size = decode_yuv(stream)
        keys = [i for i, entry in enumerate(info['index']) if entry & flag]
        gaps = [b - a for a, b in zip(keys, keys[1:] + [info['frames']])]
        report['streams'].append({'width': stream['width'], 'coded_pictures': len(raw) // size, 'keyframes': len(keys),
                                  'max_keyframe_interval_frames': max(gaps),
                                  'video_bytes': sum(map(len, stream['packets']))})
    return report


def _final_index(block, adpcm):
    index = block[2]
    for byte in block[4:]:
        for code in (byte & 15, byte >> 4):
            index = max(0, min(88, index + adpcm['INDEX_STEP'][code & 7]))
    return index


def check_fvid(data, key):
    from PIL import Image
    magic, fps, count, bottom, audio = struct.unpack_from('<4sIIII', data)
    assert magic == b'FVID' and fps == 20 and 0 < count < 20000 and bottom in (0, 1) and audio in (0, 1), key
    offsets = struct.unpack_from('<' + 'I' * count, data, 20)
    assert offsets[0] == 20 + 4 * count, key
    for i, offset in enumerate(offsets):
        top_n, bot_n, pcm_n = struct.unpack_from('<III', data, offset)
        assert 0 < top_n <= 128 * 1024 and 0 <= bot_n <= 128 * 1024 and bool(bot_n) == bool(bottom), key
        assert pcm_n == (3200 if audio else 0), key
        end = offset + 12 + top_n + bot_n + pcm_n
        assert end == (offsets[i + 1] if i + 1 < count else len(data)), (key, i)
        if i % 20 == 0 or i == count - 1:
            for start, length, size in [(offset + 12, top_n, (400, 240)), (offset + 12 + top_n, bot_n, (320, 240))]:
                if length:
                    image = Image.open(io.BytesIO(data[start:start + length]))
                    image.load()
                    assert image.size == size, (key, i, image.size)
    return {'id': key, 'format': 'FVID', 'frames': count, 'duration': count / fps, 'bottom': bool(bottom), 'audio': bool(audio),
            'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def check(data, key):
    return check_fvd2(data, key) if data[:4] == b'FVD2' else check_fvid(data, key)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--dir', type=Path, help='check every *.fvid in this directory instead of the published manifest')
    ap.add_argument('--json', type=Path, help='write the result here')
    a = ap.parse_args()
    reports = []
    if a.dir:
        known = {r['file']: r for r in json.loads((a.dir / 'report.json').read_text())['files']} if (a.dir / 'report.json').exists() else {}
        for path in sorted(a.dir.glob('*.fvid')):
            data = path.read_bytes()
            if path.name in known:
                assert len(data) == known[path.name]['bytes'] and hashlib.sha256(data).hexdigest() == known[path.name]['sha256'], path.name
            reports.append(check(data, path.stem))
        output = a.json
    else:
        manifest = json.loads((ROOT / '3ds/romfs/media.json').read_text())
        for key, entry in manifest.items():
            data = (ROOT / 'assets/store-resources' / entry['url'].rsplit('/', 1)[1]).read_bytes()
            assert len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256'], key
            reports.append(check(data, key))
        output = a.json or ROOT / '3ds/build/gen/media-check.json'
    result = {'files': reports, 'frames': sum(x['frames'] for x in reports), 'bytes': sum(x['bytes'] for x in reports),
              'checked': 'FVD2: header, index, every record, keyframe flags, ADPCM state, full ffmpeg Theora decode. '
                         'FVID: tables, every chunk boundary, first/last and every twentieth JPEG.'}
    if output: output.write_text(json.dumps(result, indent=2) + '\n')
    formats = sorted({x['format'] for x in reports})
    print('PASS:', len(reports), '/'.join(formats), 'files,', result['frames'], 'frame records,', result['bytes'], 'bytes')


if __name__ == '__main__':
    main()
