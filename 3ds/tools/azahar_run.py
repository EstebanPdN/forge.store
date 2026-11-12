#!/usr/bin/env python3
"""Runs the native store in Azahar with an isolated profile and scripted input.

Usage: 3ds/tools/.venv/bin/python -I 3ds/tools/azahar_run.py --name boot --seconds 40 \
           --input "30:sboot;200:sintro;260:kA;400:shome;420:q" [--new3ds] [--settings '{...}']

The script goes to sdmc:/3ds/forge-store/test.txt (see main.c for the commands). The store
dumps both screens itself (shots/*.raw); they are converted to PNG in /tmp/forge-az/<name>/.
Uses the hidden copy of Azahar (no Dock icon, no focus) and disables the Mac's controls.
Emulator timings are not hardware timings: use the SELECT overlay on a real console.
"""
import argparse
import os
import shutil
import signal
import subprocess
import time
import calendar
import struct
import zlib
import hashlib
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
AZ = Path(os.environ.get('FORGE_AZAHAR', str(Path.home() / 'Applications/azahar/azahar-macos-arm64-2126.2/Azahar.app/Contents/MacOS/azahar')))
AZH = Path.home() / 'Applications/azahar/hidden/Azahar.app/Contents/MacOS/azahar'
if AZH.exists() and not os.environ.get('FORGE_AZ_VISIBLE') and not os.environ.get('FORGE_AZAHAR'):
    AZ = AZH
NULL_KEYS = ['button_a', 'button_b', 'button_x', 'button_y', 'button_up', 'button_down', 'button_left', 'button_right',
             'button_l', 'button_r', 'button_start', 'button_select', 'button_debug', 'button_gpio14', 'button_zl',
             'button_zr', 'button_home', 'button_power', 'circle_pad', 'c_stick', 'motion_device', 'touch_device']


def raw_to_png(raw: Path, png: Path):
    """Top (400x240) over bottom (320x240), from the rotated BGR framebuffers."""
    data = raw.read_bytes()
    img = Image.new('RGB', (400, 480), (24, 16, 18))
    px = img.load()
    for y in range(240):
        for x in range(400):
            o = (x * 240 + (239 - y)) * 3
            px[x, y] = (data[o + 2], data[o + 1], data[o])
    base = 400 * 240 * 3
    for y in range(240):
        for x in range(320):
            o = base + (x * 240 + (239 - y)) * 3
            px[x + 40, y + 240] = (data[o + 2], data[o + 1], data[o])
    img.save(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--name', default='test')
    ap.add_argument('--seconds', type=int, default=60)
    ap.add_argument('--input', default='')
    ap.add_argument('--image', default=str(ROOT / 'build/forge-store.3dsx'))
    ap.add_argument('--new3ds', action='store_true')
    ap.add_argument('--seed-media', type=Path, help='Copy a test video cache into the isolated SD before boot')
    ap.add_argument('--seed-reader', type=Path, help='Copy public GitHub response cache fixtures into the isolated SD')
    ap.add_argument('--seed-universal',type=Path,help='Seed an explicitly converted Universal-DB cache fixture')
    ap.add_argument('--seed-sd',type=Path,help='Copy fixture files into the isolated SD root (never the user SD)')
    ap.add_argument('--record', action='store_true', help='Request recording.webm (Vulkan may capture audio only)')
    ap.add_argument('--renderer', choices=['vulkan', 'opengl'], default='vulkan')
    ap.add_argument('--clock', type=int, help='Fixed Unix time to test certificate clock recovery')
    ap.add_argument('--settings', default='', help='settings.json to start with (skips the theme picker)')
    a = ap.parse_args()
    out = Path('/tmp/forge-az') / a.name
    shutil.rmtree(out, ignore_errors=True)
    rt = out / 'rt'
    user = rt / 'xdg-data/azahar-emu'
    cfg = rt / 'xdg-config/azahar-emu'
    sd = user / 'sdmc/3ds/forge-store'
    sd.mkdir(parents=True)
    cfg.mkdir(parents=True)
    if a.seed_sd:shutil.copytree(a.seed_sd,sd.parents[1],dirs_exist_ok=True)
    watched=sorted(set(ROOT.glob('source/*.[ch]'))|set(ROOT.glob('source/*.inc'))|set(ROOT.glob('source/vendor/*.[ch]'))|set(ROOT.glob('source/vendor/*.inc')))
    image=Path(a.image)
    stale=[str(p.relative_to(ROOT)) for p in watched if p.stat().st_mtime>image.stat().st_mtime]
    if stale:raise SystemExit('Rebuild before capturing: '+', '.join(stale))
    # Azahar reads RomFS lazily from this file for the lifetime of the process.
    # Rebuilding the shared binary must never alter a running test's resources.
    image_hash=hashlib.sha256(image.read_bytes()).hexdigest()
    execution_image=out/('execution-image'+image.suffix)
    shutil.copy2(image,execution_image)
    if hashlib.sha256(execution_image.read_bytes()).hexdigest()!=image_hash:raise SystemExit('Build changed while copying; retry after it completes')
    snapshot={'image':str(image),'execution_image':str(execution_image),'image_sha256':image_hash,'new3ds':a.new3ds,'settings':json.loads(a.settings) if a.settings else None,'script':a.input,'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}}
    (out/'build-snapshot.json').write_text(json.dumps(snapshot,indent=2)+'\n')
    (rt / 'xdg-cache').mkdir()
    (sd / 'test.txt').write_text(a.input.replace(';', '\n') + '\n')
    if a.seed_media:
        shutil.copytree(a.seed_media, sd / 'cache/videos', dirs_exist_ok=True)
    if a.seed_reader:
        for fixture in a.seed_reader.glob('gh-*.cache'):
            body=fixture.read_bytes()
            if body.startswith(b'FGH2'):body=body[20:]
            # Explicit public-response fixtures get a fresh console-local RTC
            # timestamp. Product cache expiry is tested separately on the host.
            stamp=calendar.timegm(time.localtime())
            header=struct.pack('<4sQII',b'FGH2',stamp,len(body),zlib.crc32(body))
            (sd / fixture.name).write_bytes(header+body)
    if a.seed_universal:
        body=a.seed_universal.read_bytes()
        assert isinstance(json.loads(body),list)
        stamp=calendar.timegm(time.localtime())
        (sd/'universal-v2.cache').write_bytes(struct.pack('<4sQII',b'FGH2',stamp,len(body),zlib.crc32(body))+body)
    if a.settings:
        (sd / 'settings.json').write_text(a.settings)
    fw = Path.home() / 'Library/Application Support/Azahar/sdmc/3ds/dspfirm.cdc'
    if fw.exists():
        shutil.copy(fw, sd.parent / 'dspfirm.cdc')
    controls = ('[Controls]\nprofile=0\nprofiles\\size=1\nprofiles\\1\\name=Default\n'
                + ''.join(f'profiles\\1\\{k}=engine:null\nprofiles\\1\\{k}\\default=false\n' for k in NULL_KEYS)
                + 'profiles\\1\\use_touch_from_button=false\nprofiles\\1\\use_touchpad=false\n')
    (cfg / 'qt-config.ini').write_text(
        f'[System]\nis_new_3ds={"true" if a.new3ds else "false"}\nis_new_3ds\\default=false\n'
        + (f'init_clock=1\ninit_clock\\default=false\ninit_time={a.clock}\ninit_time\\default=false\n' if a.clock is not None else '') +
        '[UI]\nfirstStart=false\ncalloutFlags=4294967295\nconfirmClose=false\nconfirmClose\\default=false\n'
        '[Audio]\noutput_type=1\noutput_type\\default=false\n'
        '[Miscellaneous]\ncheck_for_update_on_start=false\ncheck_for_update_on_start\\default=false\n'
        'log_filter=*:Info\nlog_filter\\default=false\n'
        '[Debugging]\ninstant_debug_log=true\ninstant_debug_log\\default=false\n'
        f'[Renderer]\ngraphics_api={2 if a.renderer == "vulkan" else 1}\ngraphics_api\\default=false\n'
        'filter_mode=false\nfilter_mode\\default=false\n'
        '[WebService]\nenable_telemetry=false\n' + controls)
    env = {**os.environ, 'CFFIXED_USER_HOME': str(rt), 'XDG_DATA_HOME': str(rt / 'xdg-data'),
           'XDG_CONFIG_HOME': str(rt / 'xdg-config'), 'XDG_CACHE_HOME': str(rt / 'xdg-cache')}
    ffmpeg_lib = Path('/opt/homebrew/opt/ffmpeg@6/lib')
    if a.record and ffmpeg_lib.exists():
        env['DYLD_FALLBACK_LIBRARY_PATH'] = str(ffmpeg_lib)
    log = open(out / 'azahar.out', 'w')
    # macOS Azahar opens a modal warning when its executable has a parent other
    # than launchd. Launch the bundle and explicitly pass the isolated profile.
    launch = ['/usr/bin/open', '-n', '-g', '-W', '-a', str(AZ.parents[2]),
              '--stdout', str(out / 'azahar.out'), '--stderr', str(out / 'azahar.out')]
    for key in ('CFFIXED_USER_HOME', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME'):
        launch.extend(['--env', f'{key}={env[key]}'])
    if a.record and ffmpeg_lib.exists():
        launch.extend(['--env', f'DYLD_FALLBACK_LIBRARY_PATH={ffmpeg_lib}'])
    launch.append('--args')
    if a.record:
        # Bypass the utility CLI's getopt parser; Qt owns these options.
        launch.extend(['--', '--windowed', '--dump-video', str(out / 'recording.webm')])
    launch.append(str(execution_image))
    p = subprocess.Popen(launch, env=env, stdout=log, stderr=log, cwd=rt, start_new_session=True)
    t0 = time.time()
    reason = 'timeout'
    while time.time() - t0 < a.seconds:
        time.sleep(0.5)
        if p.poll() is not None:
            reason = 'Azahar exited'
            break
        if (sd / 'done.txt').exists():
            reason = 'script finished'
            time.sleep(0.5)
            break
    # The app is owned by launchd, not by `open`; stop only the process carrying
    # this run's unique profile, never an emulator session opened by the user.
    listing = subprocess.check_output(['ps', '-axo', 'pid=,comm='], text=True)
    for line in listing.splitlines():
        fields = line.strip().split(None, 1)
        if len(fields) != 2 or fields[1] != str(AZ):
            continue
        pid = int(fields[0])
        proc = subprocess.run(['ps', 'eww', '-p', str(pid), '-o', 'command='], capture_output=True, text=True)
        if f'CFFIXED_USER_HOME={rt} ' not in proc.stdout + ' ':
            continue
        try:
            os.kill(pid, signal.SIGTERM)
            p.wait(5)
        except ProcessLookupError:
            pass
        except subprocess.TimeoutExpired:
            os.kill(pid, signal.SIGKILL)
    if p.poll() is None:
        p.terminate()
        p.wait(5)
    log.close()
    shots = sorted((sd / 'shots').glob('*.raw')) if (sd / 'shots').exists() else []
    for s in shots:
        raw_to_png(s, out / (s.stem + '.png'))
    if (sd / 'log.txt').exists():
        shutil.copy(sd / 'log.txt', out / 'log.txt')
    print(f'[azahar] {reason} after {time.time() - t0:.1f} s, {len(shots)} shots in {out}')
    if a.record:
        recording = out / 'recording.webm'
        print(f'[recording] {recording} ({recording.stat().st_size} bytes)' if recording.exists() else '[recording] unavailable; inspect Azahar log')
    if (out / 'log.txt').exists():
        print((out / 'log.txt').read_text()[-2500:])
    else:
        print('(no log.txt)')
        print(open(out / 'azahar.out').read()[-2500:])


if __name__ == '__main__':
    main()
