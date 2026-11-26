#!/usr/bin/env python3
"""Copy a reviewed public build, its checksums and source fingerprints. Never publishes."""
from pathlib import Path
import hashlib,json,shutil,re
ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'3ds/build'; dest=ROOT/'builds/3ds'
flags=(source/'compile-config.txt').read_text()
if '-DFORGE_DEBUG=0' not in flags or 'FORGE_DUMP_URL' in flags:
    raise SystemExit('Rebuild with FORGE_DEBUG=0 and without FORGE_DUMP_URL before packaging')
# Check both finished packages before copying either of them. RomFS directory
# entries use UTF-16LE. Staging alone cannot identify a stale personal binary.
code_catalog = ROOT/'3ds/romfs/forge-code-assets.fme'
for name in ('forge-store.cia','forge-store.3dsx'):
    binary=(source/name).read_bytes()
    if any(entry.encode('utf-16le') in binary for entry in ('personal-assets.fme','forge-default.key')):
        raise SystemExit('Personal RomFS in '+name+'; rebuild the public edition before packaging')
    if 'forge-code-assets.fme'.encode('utf-16le') not in binary or code_catalog.read_bytes() not in binary:
        raise SystemExit('Missing approved code-activated catalog in '+name)
watched=sorted([* (ROOT/'3ds/source').rglob('*.c'),*(ROOT/'3ds/source').rglob('*.h'),*(ROOT/'3ds/source').rglob('*.inc'),ROOT/'3ds/build.sh',ROOT/'3ds/forge-store.rsf',ROOT/'3ds/tools/stage-romfs.py',ROOT/'3ds/tools/store_packs.py',ROOT/'3ds/tools/vorbis-encode.c',code_catalog])
oldest=min((source/n).stat().st_mtime for n in ['forge-store.cia','forge-store.3dsx'])
if any(p.stat().st_mtime>oldest for p in watched):raise SystemExit('Source changed after compilation; rebuild')
dest.mkdir(parents=True,exist_ok=True)
build_number=int(re.search(r'#define STORE_BUILD (\d+)',(ROOT/'3ds/source/store_version.h').read_text())[1])
display_version=re.search(r'#define STORE_VERSION "([^"]+)"',(ROOT/'3ds/source/store_version.h').read_text())[1]
manifest={'store_build':build_number,'version':display_version,'kind':'public build','debug_dumps':False,'dump_upload':False,'files':{},'source_sha256':{},'media_manifest_sha256':hashlib.sha256((ROOT/'3ds/romfs/media.json').read_bytes()).hexdigest()}
for name in ['forge-store.cia','forge-store.3dsx']:
    shutil.copy2(source/name,dest/name)
    data=(dest/name).read_bytes()
    manifest['files'][name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
for p in watched:manifest['source_sha256'][p.relative_to(ROOT).as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(dest/'update.json').write_text(json.dumps({k:manifest[k] for k in ('store_build','version','files')},indent=2)+'\n')
(dest/'SHA256SUMS').write_text(''.join(f"{v['sha256']}  {k}\n" for k,v in manifest['files'].items()))
print('Packaged public CIA and 3DSX with source/media fingerprints')
