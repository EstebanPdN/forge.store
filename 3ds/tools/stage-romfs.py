#!/usr/bin/env python3
"""Stage the CIA's RomFS from the checked-in 3ds/romfs (which stays unchanged).

- Only my ports' banners stay inside, as one sequential bank instead of many small reads.
  Every other banner, gallery, poster, install backdrop and bundled GitHub page or image
  goes into a resource pack (store_packs.py) that the store downloads after it starts.
- Music and sounds become Ogg Vorbis (32 kHz stereo, decoded by Tremor on the console).
- Fonts, kerning and the large JSON files are gzip-compressed; the store reads them with
  zlib, which also accepts the plain files.
Every step is deterministic: the same inputs give byte-identical outputs.
"""
import gzip, hashlib, importlib.util, json, os, shutil, struct, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
source = ROOT / '3ds/romfs'
target = ROOT / '3ds/build/romfs'
tools = ROOT / '3ds/build/tools'
spec = importlib.util.spec_from_file_location('store_packs', ROOT / '3ds/tools/store_packs.py')
store_packs = importlib.util.module_from_spec(spec); spec.loader.exec_module(store_packs)

VORBIS_QUALITY = '0.4'  # about 80 kbit/s: transparent on the console speakers, close on headphones
GZIP = ('gfx/fonts.bin', 'gfx/kerning.bin', 'catalog.json', 'strings.json', 'universal.json')

if target.exists(): shutil.rmtree(target)
moved = {Path(m).name for m in store_packs.MOVED}
shutil.copytree(source, target, ignore=shutil.ignore_patterns('banner', '.DS_Store', 'forge-default.key', 'personal-assets.fme', *moved))
if private_manifest := os.environ.get('FORGE_PERSONAL_MANIFEST'):
    sealed = Path(private_manifest).read_bytes()
    if len(sealed) < 56 or len(sealed) > 56 + 192 * 1024 or sealed[:4] != b'FM01':
        raise ValueError('Expected a sealed Forge Mode manifest')
    (target / 'personal-assets.fme').write_bytes(sealed)
if activation := os.environ.get('FORGE_DEFAULT_ACTIVATION'):
    if not private_manifest:
        raise ValueError('FORGE_DEFAULT_ACTIVATION requires FORGE_PERSONAL_MANIFEST')
    saved = Path(activation).read_bytes()
    if len(saved) != 68 or saved[:4] != b'FMK1' or saved[4:36] != hashlib.sha256(sealed).digest():
        raise ValueError('Activation does not match the personal manifest')
    (target / 'forge-default.key').write_bytes(saved)
for folder in store_packs.MOVED:
    assert not (target / folder).exists(), folder

# ---- my ports' banners: one bank ----------------------------------------------------------
apps = json.loads((source / 'catalog.json').read_text())['apps']
files = store_packs.core_banner_files()
start = 12 + 72 * len(files); index = bytearray(); payload = bytearray()
meta = json.loads((target / 'gfx/banners.json').read_text())
for app in apps:
    meta.setdefault(app['id'], {})['_bytes'] = 0
# Texture bytes of every catalog banner (in the bank or in a pack) for the GPU budget.
for app in apps:
    for layer in ('bg', 'fg'):
        p = source / 'gfx/banner' / f"{app['id']}-{layer}.ftx"
        if p.exists():
            data = p.read_bytes(); at = 12 + data[9] * 8
            meta[app['id']]['_bytes'] += struct.unpack_from('<I', data, at)[0]
for p in files:
    data = p.read_bytes(); name = p.name.encode(); assert len(name) < 64
    index.extend(struct.pack('<64sII', name, start + len(payload), len(data))); payload.extend(data)
bank = struct.pack('<4sII', b'FBN1', len(files), start + len(payload)) + index + payload
assert len(bank) <= 8 * 1024 * 1024
(target / 'gfx/banner-bank.bin').write_bytes(bank)
(target / 'gfx/banners.json').write_text(json.dumps(meta, separators=(',', ':')) + '\n')
print(f'Banner bank: {len(files)} sheets of my ports, {len(bank)} bytes')

# ---- sound: Ogg Vorbis ----------------------------------------------------------------------
encoder = tools / 'vorbis-encode'
encoder_source = ROOT / '3ds/tools/vorbis-encode.c'
if not encoder.exists() or encoder.stat().st_mtime < encoder_source.stat().st_mtime:
    tools.mkdir(parents=True, exist_ok=True)
    subprocess.run(['clang', '-O2', '-I/opt/homebrew/include', str(encoder_source), '-L/opt/homebrew/lib',
                    '-lvorbisenc', '-lvorbis', '-logg', '-o', str(encoder)], check=True)
cache = ROOT / '3ds/build/audio-cache'; cache.mkdir(parents=True, exist_ok=True)
before = after = 0
for pcm in sorted((target / 'audio').glob('*.pcm')):
    raw = pcm.read_bytes()
    key = hashlib.sha256(raw + VORBIS_QUALITY.encode() + encoder_source.read_bytes()).hexdigest()[:16]
    cached = cache / f'{pcm.stem}-{key}.ogg'
    if not cached.exists():
        subprocess.run([str(encoder), VORBIS_QUALITY, str(pcm), str(cached)], check=True)
    shutil.copyfile(cached, pcm.with_suffix('.ogg')); pcm.unlink()
    before += len(raw); after += cached.stat().st_size
print(f'Audio: {before} → {after} bytes (Ogg Vorbis q{VORBIS_QUALITY})')

# ---- gzip ------------------------------------------------------------------------------------
for rel in GZIP:
    p = target / rel; raw = p.read_bytes()
    p.write_bytes(gzip.compress(raw, compresslevel=9, mtime=0))
    print(f'{rel}: {len(raw)} → {p.stat().st_size} bytes (gzip)')

# ---- resource packs ------------------------------------------------------------------------------
manifest = store_packs.build()
(target / 'packs.json').write_bytes(store_packs.manifest_bytes(manifest))
print(f"Resource packs: {len(manifest['packs'])} packs, {sum(p['bytes'] for p in manifest['packs'])} bytes downloaded after install")
