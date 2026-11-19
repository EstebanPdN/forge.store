#!/usr/bin/env python3
"""The production music reader (music_stream.h) with the real build encoder.

The console decodes with Tremor; this harness compiles the same header against
libvorbisfile. It checks the exact length of every encoded sound, the opening→loop seam,
two loops, a restart, and that wrong formats, damage and missing/empty loops stop the
stream instead of spinning."""
from pathlib import Path
import array, math, shutil, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
QUALITY = '0.4'
source = r'''
#include "music_stream.h"
#include <assert.h>
#include <stdlib.h>
static unsigned char *decode_all(const char *path, size_t *n) {
    MusicStream s = {0};
    assert(music_stream_file(&s, path));
    long frames = music_stream_frames(&s);
    assert(frames > 0);
    unsigned char *p = malloc(frames * 4);
    size_t at = 0;
    long got;
    while ((got = music_stream_raw(&s, p + at, 4096 < frames * 4 - (long)at ? 4096 : (int)(frames * 4 - at))) > 0) at += got;
    assert(got == 0 && at == (size_t)frames * 4);
    music_stream_close(&s);
    *n = at;
    return p;
}
int main(int argc, char **argv) {
    if (argc == 3) {  // exact length of one encoded sound
        MusicStream s = {0};
        assert(music_stream_file(&s, argv[1]));
        assert(music_stream_frames(&s) == atol(argv[2]));
        size_t n; free(decode_all(argv[1], &n)); assert(n == (size_t)atol(argv[2]) * 4);
        music_stream_close(&s);
        return 0;
    }
    size_t a, b;
    unsigned char *opening = decode_all("start.ogg", &a), *loop = decode_all("loop.ogg", &b);
    unsigned char block[16384];
    MusicStream s = {0};
    for (int restart = 0; restart < 2; restart++) {
        assert(music_stream_open(&s, "start.ogg", "loop.ogg"));
        for (size_t pos = 0; pos < a + 2 * b; pos += sizeof block) {
            assert(music_stream_read(&s, block, sizeof block));
            for (size_t i = 0; i < sizeof block; i++) {
                size_t at = pos + i;
                assert(block[i] == (at < a ? opening[at] : loop[(at - a) % b]));
            }
        }
    }
    music_stream_close(&s); music_stream_close(&s);
    assert(!music_stream_open(&s, "missing.ogg", "loop.ogg"));
    const char *bad[] = {"empty.ogg", "truncated-head.ogg", "mono.ogg", "rate.ogg", "garbage.ogg"};
    for (unsigned k = 0; k < sizeof bad / sizeof *bad; k++) assert(!music_stream_open(&s, bad[k], "loop.ogg"));
    // Damage in the middle of the opening, a missing loop and an empty loop all end the stream.
    const char *ends[][2] = {{"corrupt.ogg", "loop.ogg"}, {"start.ogg", "missing.ogg"}, {"start.ogg", "empty.ogg"}};
    for (unsigned k = 0; k < 3; k++) {
        bool failed = !music_stream_open(&s, ends[k][0], ends[k][1]);
        for (size_t pos = 0; !failed && pos < a + b + 32768; pos += sizeof block)
            if (!music_stream_read(&s, block, sizeof block)) failed = true;
        assert(failed);
        music_stream_close(&s);
    }
    free(opening); free(loop);
    puts("seams ok");
    return 0;
}
'''

def tone(path, seconds, rate=32000, channels=2):
    samples = array.array('h')
    for i in range(int(seconds * rate)):
        v = int(9000 * math.sin(i * 2 * math.pi * 440 / rate))
        samples.extend([v] * channels)
    path.write_bytes(samples.tobytes())

with tempfile.TemporaryDirectory(prefix='forge-music-') as temp:
    p = Path(temp)
    encoder = p / 'vorbis-encode'
    subprocess.run(['clang', '-O2', '-I/opt/homebrew/include', str(root / 'tools/vorbis-encode.c'), '-L/opt/homebrew/lib',
                    '-lvorbisenc', '-lvorbis', '-logg', '-o', str(encoder)], check=True)
    (p / 'test.c').write_text(source)
    exe = p / 'test'
    subprocess.run(['clang', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-DMUSIC_STREAM_HOST',
                    '-I/opt/homebrew/include', '-I' + str(root / 'source'), str(p / 'test.c'), '-L/opt/homebrew/lib',
                    '-lvorbisfile', '-lvorbis', '-logg', '-o', str(exe)], check=True)
    # Every shipped sound keeps its exact length (loop timing and effect ends).
    sounds = sorted((root / 'romfs/audio').glob('*.pcm'))
    for pcm in sounds:
        ogg = p / (pcm.stem + '.ogg')
        subprocess.run([str(encoder), QUALITY, str(pcm), str(ogg)], check=True)
        subprocess.run([str(exe), str(ogg), str(pcm.stat().st_size // 4)], check=True)
    shutil.copyfile(p / 'music-start.ogg', p / 'start.ogg'); shutil.copyfile(p / 'music.ogg', p / 'loop.ogg')
    start = (p / 'start.ogg').read_bytes()
    (p / 'empty.ogg').write_bytes(b'')
    (p / 'truncated-head.ogg').write_bytes(start[:200])
    (p / 'garbage.ogg').write_bytes(bytes(range(256)) * 64)
    damaged = bytearray(start); damaged[len(damaged) // 2:len(damaged) // 2 + 4096] = b'\0' * 4096
    (p / 'corrupt.ogg').write_bytes(damaged)
    tone(p / 'mono.pcm', 1, channels=1)
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 's16le', '-ar', '32000', '-ac', '1', '-i', str(p / 'mono.pcm'),
                    '-c:a', 'libopus', '-f', 'ogg', str(p / 'mono-opus.ogg')], check=True)
    shutil.copyfile(p / 'mono-opus.ogg', p / 'mono.ogg')  # not Vorbis at all
    tone(p / 'rate.pcm', 1, rate=44100)
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 's16le', '-ar', '44100', '-ac', '2', '-i', str(p / 'rate.pcm'),
                    '-c:a', 'vorbis', '-strict', '-2', '-f', 'ogg', str(p / 'rate.ogg')], check=True)
    subprocess.run([str(exe)], cwd=p, check=True, capture_output=True)
    print(f'PASS: {len(sounds)} sounds keep their exact length; opening/loop seams, two loops, restart, '
          'wrong format/rate, damage, empty and missing files; ASan/UBSan')
