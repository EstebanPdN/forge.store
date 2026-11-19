#!/usr/bin/env python3
"""Production assets.c against real files: fresh, installed and invalid manifests.

Also builds the real pack plan from 3ds/romfs and checks that every pack reproduces the
original bytes of every file it carries."""
from pathlib import Path
import gzip, hashlib, importlib.util, json, subprocess, tempfile
root = Path(__file__).resolve().parents[1]

def manifest(packs):
    return {'format': 1, 'packs': packs}

def pack(pack_id, files):
    data, index = bytearray(), []
    for rel, blob in files:
        index.append([rel, len(data), len(blob)]); data += blob
    sha = hashlib.sha256(data).hexdigest()
    entry = {'id': pack_id, 'file': f'{pack_id}-{sha[:12]}.fpk', 'url': 'https://raw.githubusercontent.com/x/y/z/' + pack_id,
             'sha256': sha, 'bytes': len(data), 'files': index}
    return entry, bytes(data)

def fixture(p, packs, install=(), extra=()):
    (p / 'romfs:').mkdir(); (p / 'packs').mkdir(); (p / 'staged').mkdir()
    (p / 'romfs:/core.txt').write_text('inside the CIA')
    (p / 'romfs:/core.json.gz').write_bytes(gzip.compress(b'{"ok":7}', mtime=0))
    (p / 'romfs:/plain.json').write_text('{"plain":true}')
    (p / 'romfs:/packs.json').write_bytes(gzip.compress(json.dumps(manifest([e for e, _ in packs])).encode(), mtime=0))
    for entry, data in packs:
        (p / 'staged' / f"{entry['id']}.fpk").write_bytes(data)
    for i, size in install:
        entry, data = packs[i]
        (p / 'packs' / entry['file']).write_bytes(data[:size] if size is not None else data)
    for name in extra:
        (p / 'packs' / name).write_bytes(b'stale')

def main():
    banners = pack('banners-1', [('gfx/one.ftx', b'first file'), ('gfx/two.ftx', b'second.ftx')])
    pages = pack('github-pages-1', [('reader-data/page', b'0123456789'),
                                    ('reader-data/page.json', gzip.compress(b'{"page":1}', mtime=0))])
    invalid = []
    for broken in ({'sha256': 'XYZ'}, {'bytes': 0}, {'url': 'http://example.com/a'}, {'id': '../x'},
                   {'file': 'a/b.fpk'}):
        entry = dict(banners[0], **broken); invalid.append([(entry, banners[1]), pages])
    entry = dict(banners[0], files=[['../escape', 0, 4]]); invalid.append([(entry, banners[1]), pages])
    entry = dict(banners[0], files=[['gfx/one.ftx', 15, 10]]); invalid.append([(entry, banners[1]), pages])
    entry = dict(pages[0], files=[['gfx/one.ftx', 0, 4]]); invalid.append([banners, (entry, pages[1])])  # duplicate path
    with tempfile.TemporaryDirectory(prefix='forge-assets-') as temp:
        t = Path(temp)
        common = ['-std=gnu11', '-g', '-O1', '-Wall', '-Werror', '-DASSETS_HOST_TEST', '-I/opt/homebrew/include',
                  str(root / 'tests/assets-test.c'), str(root / 'source/assets.c'),
                  '-L/opt/homebrew/lib', '-ljansson', '-lz', '-lpthread']
        builds = {'asan': ['-fsanitize=address,undefined'], 'tsan': ['-fsanitize=thread']}
        for name, flags in builds.items():
            subprocess.run(['clang', *flags, *common, '-o', str(t / name)], check=True)
        runs = [('fresh', [banners, pages], (), ()),
                ('installed', [banners, pages], ((0, None), (1, 5)), ('old-000000000000.fpk', 'x.fpk.part'))]
        runs += [('invalid', case, (), ()) for case in invalid]
        runs += [('nomanifest', None, (), ())]
        for i, (mode, packs, install, extra) in enumerate(runs):
            for name in builds:
                p = t / f'{i}-{name}'; p.mkdir(); fixture(p, packs or [banners], install, extra)
                if packs is None: (p / 'romfs:/packs.json').unlink()
                subprocess.run([str(t / name), mode], cwd=p, check=True, capture_output=True, timeout=60)
    # The real plan: every packed file is byte-identical to its RomFS original.
    spec = importlib.util.spec_from_file_location('store_packs', root / 'tools/store_packs.py')
    store_packs = importlib.util.module_from_spec(spec); spec.loader.exec_module(store_packs)
    built = store_packs.build()
    files = 0
    for entry in built['packs']:
        data = (store_packs.OUT / entry['file']).read_bytes()
        assert hashlib.sha256(data).hexdigest() == entry['sha256'] and len(data) == entry['bytes']
        assert entry['file'] == f"{entry['id']}-{entry['sha256'][:12]}.fpk"
        for rel, offset, size in entry['files']:
            assert data[offset:offset + size] == (store_packs.ROMFS / rel).read_bytes(), rel
            files += 1
    again = store_packs.build()
    assert again == built, 'pack build is not deterministic'
    print(f'PASS: assets.c fresh/installed/missing/{len(invalid)} invalid manifests (ASan/UBSan, TSan); '
          f'{len(built["packs"])} real packs reproduce {files} RomFS files byte for byte, deterministically')

main()
