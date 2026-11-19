#!/usr/bin/env python3
"""Store resource packs: what stays in the CIA and what the store downloads afterwards.

The checked-in 3ds/romfs stays the single source of truth. This module decides which
files ship in RomFS (the interface, fonts, sounds, icons and my ports' banners) and
groups everything else into immutable packs, in the order the store downloads them:

  1. banners of the other curated apps (seen on Home),
  2. galleries, video posters and install art per app (my ports first),
  3. Universal-DB banners (most-starred first; Universal-DB is optional),
  4. bundled GitHub pages, then their images.

Videos keep their own manifest and come last. A pack is the plain concatenation of its
files; its name carries the first 12 hex digits of its SHA-256, so unchanged content keeps
its name and is never downloaded twice. Packs are published on the `store-packs` branch.

  python3 3ds/tools/store_packs.py build     # write 3ds/build/store-packs and the manifest
  python3 3ds/tools/store_packs.py publish   # add new packs to origin/store-packs
  python3 3ds/tools/store_packs.py verify    # check every URL serves the exact bytes
"""
from pathlib import Path
import gzip, hashlib, json, subprocess, sys, tempfile, urllib.request

ROOT = Path(__file__).resolve().parents[2]
ROMFS = ROOT / '3ds/romfs'
OUT = ROOT / '3ds/build/store-packs'
REPO = 'EstebanPdN/forge.store'
BRANCH = 'store-packs'
BASE_URL = f'https://raw.githubusercontent.com/{REPO}/{BRANCH}/'
TARGET = 1024 * 1024  # bytes per pack before starting the next one of the same kind
# RomFS folders that move entirely into packs (banner/ is split: my ports stay inside).
MOVED = ('gfx/universal', 'gfx/shots', 'gfx/install', 'video', 'reader-art', 'reader-data')


def catalog_apps():
    return json.loads((ROMFS / 'catalog.json').read_text())['apps']


def my_ports():
    """My ports keep their banners in the CIA: the ones with an assets/ports folder."""
    return {p.name for p in (ROOT / 'assets/ports').iterdir() if p.is_dir()}


def core_banner_files():
    mine = my_ports()
    return [p for app in catalog_apps() if app['id'] in mine
            for layer in ('bg', 'fg') if (p := ROMFS / 'gfx/banner' / f"{app['id']}-{layer}.ftx").exists()]


def _chunks(prefix, files):
    packs, current, size = [], [], 0
    for f in files:
        if current and size + f.stat().st_size > TARGET:
            packs.append(current)
            current, size = [], 0
        current.append(f)
        size += f.stat().st_size
    if current:
        # A small remainder joins the previous pack instead of becoming its own download.
        if packs and size < TARGET // 4:
            packs[-1] += current
        else:
            packs.append(current)
    return [(f'{prefix}-{i + 1}', group) for i, group in enumerate(packs)]


def plan():
    """[(pack id, [absolute paths])] in download order."""
    apps = catalog_apps()
    mine = my_ports()
    order = sorted(range(len(apps)), key=lambda i: (apps[i]['id'] not in mine, i))
    packs = []
    banners = [ROMFS / 'gfx/banner' / f"{apps[i]['id']}-{layer}.ftx" for i in order if apps[i]['id'] not in mine
               for layer in ('bg', 'fg')]
    packs += _chunks('banners', [p for p in banners if p.exists()])
    for i in order:
        app = apps[i]['id']
        files = sorted((ROMFS / 'gfx/shots').glob(f'{app}-*.ftx'))
        files += [p for p in (ROMFS / 'video' / f'{app}-posters.ftx', ROMFS / 'video' / f'{app}-bottom-posters.ftx',
                              ROMFS / 'gfx/install' / f'{app}.ftx') if p.exists()]
        if files:
            packs.append((f'app-{app}', files))
    universal = json.loads((ROMFS / 'universal.json').read_text())['apps']
    ranked = sorted(universal, key=lambda a: (not a.get('featured'), -(a.get('stars') or 0)))
    seen = set()
    udb = []
    for a in ranked:
        p = ROMFS / 'gfx/universal' / f"{a['id']}.ftx"
        if p.exists() and p not in seen:
            udb.append(p)
            seen.add(p)
    udb += [p for p in sorted((ROMFS / 'gfx/universal').glob('*.ftx')) if p not in seen]
    packs += _chunks('udb-banners', udb)
    packs += _chunks('github-pages', sorted((ROMFS / 'reader-data').iterdir()))
    packs += _chunks('github-images', sorted((ROMFS / 'reader-art').glob('*.rimg')))
    # Nothing may be lost between the CIA and the packs.
    packed = [p for _, files in packs for p in files]
    moved = {p for folder in MOVED for p in (ROMFS / folder).rglob('*') if p.is_file() and p.name != '.DS_Store'}
    # Banners of apps no longer in the catalog were never shipped (the bank skipped them too).
    moved |= {p for app in apps for layer in ('bg', 'fg')
              if (p := ROMFS / 'gfx/banner' / f"{app['id']}-{layer}.ftx").exists()} - set(core_banner_files())
    if len(packed) != len(set(packed)) or set(packed) != moved:
        missing = sorted(str(p.relative_to(ROMFS)) for p in moved - set(packed))
        raise SystemExit(f'Pack plan disagrees with RomFS (unpacked: {missing[:5]})')
    return packs


def build():
    """Writes every pack into 3ds/build/store-packs and returns the manifest."""
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = {'format': 1, 'packs': []}
    names = set()
    for pack_id, files in plan():
        data, index = bytearray(), []
        for f in files:
            blob = f.read_bytes()
            index.append([f.relative_to(ROMFS).as_posix(), len(data), len(blob)])
            data += blob
        sha = hashlib.sha256(data).hexdigest()
        name = f'{pack_id}-{sha[:12]}.fpk'
        names.add(name)
        target = OUT / name
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
        manifest['packs'].append({'id': pack_id, 'file': name, 'url': BASE_URL + name,
                                  'sha256': sha, 'bytes': len(data), 'files': index})
    for stale in OUT.glob('*.fpk'):
        if stale.name not in names:
            stale.unlink()
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=1) + '\n')
    return manifest


def manifest_bytes(manifest):
    """The compact, gzip-compressed copy that goes into RomFS (byte-identical every build)."""
    return gzip.compress(json.dumps(manifest, separators=(',', ':')).encode(), compresslevel=9, mtime=0)


def git(*args, input=None, env=None):
    return subprocess.run(['git', *args], cwd=ROOT, input=input, env=env, check=True,
                          capture_output=True).stdout


def publish():
    """Adds the current packs to the store-packs branch (append-only) and pushes it."""
    manifest = build()
    subprocess.run(['git', 'fetch', 'origin', f'{BRANCH}:refs/remotes/origin/{BRANCH}'], cwd=ROOT,
                   capture_output=True)
    try:
        parent = git('rev-parse', '--verify', f'refs/remotes/origin/{BRANCH}').decode().strip()
        tree = git('ls-tree', parent).decode()
    except subprocess.CalledProcessError:
        parent, tree = None, ''
    existing = {line.split('\t', 1)[1] for line in tree.splitlines()}
    new = [p for p in manifest['packs'] if p['file'] not in existing]
    if not new:
        print(f'{BRANCH}: all {len(manifest["packs"])} packs already published')
        return
    entries = tree.splitlines()
    for p in new:
        blob = git('hash-object', '-w', str(OUT / p['file'])).decode().strip()
        entries.append(f"100644 blob {blob}\t{p['file']}")
    readme = ('# forge.store resource packs\n\nImmutable art and data that forge.store downloads after '
              'installation. Each file name carries the start of its SHA-256; files are only ever added.\n')
    if 'README.md' not in existing:
        blob = git('hash-object', '-w', '--stdin', input=readme.encode()).decode().strip()
        entries.append(f'100644 blob {blob}\tREADME.md')
    new_tree = git('mktree', input=('\n'.join(sorted(entries, key=lambda e: e.split('\t', 1)[1])) + '\n').encode()).decode().strip()
    message = f'Add {len(new)} resource pack{"s" if len(new) != 1 else ""}'
    commit = git('commit-tree', new_tree, *(['-p', parent] if parent else []), '-m', message).decode().strip()
    subprocess.run(['git', 'push', 'origin', f'{commit}:refs/heads/{BRANCH}'], cwd=ROOT, check=True)
    git('update-ref', f'refs/remotes/origin/{BRANCH}', commit)
    print(f'{BRANCH}: published {len(new)} new packs ({sum(p["bytes"] for p in new)} bytes) at {commit[:12]}')


def verify():
    """Downloads every pack from its public URL and checks its size and SHA-256."""
    manifest = json.loads((OUT / 'manifest.json').read_text())
    for p in manifest['packs']:
        request = urllib.request.Request(p['url'], headers={'User-Agent': 'forge.store-pack-check'})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read()
        if len(data) != p['bytes'] or hashlib.sha256(data).hexdigest() != p['sha256']:
            raise SystemExit(f"{p['file']}: published bytes differ")
    print(f"PASS: {len(manifest['packs'])} packs served with matching size and SHA-256")


if __name__ == '__main__':
    command = sys.argv[1] if len(sys.argv) > 1 else 'build'
    if command == 'build':
        m = build()
        total = sum(p['bytes'] for p in m['packs'])
        print(f"{len(m['packs'])} packs, {total} bytes, {sum(len(p['files']) for p in m['packs'])} files")
    elif command == 'publish':
        publish()
    elif command == 'verify':
        verify()
    else:
        raise SystemExit(__doc__)
