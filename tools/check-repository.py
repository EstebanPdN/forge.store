#!/usr/bin/env python3
"""Check distributable paths, manifests, catalog assets, documentation links and build hashes."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
from urllib.parse import unquote
ROOT=Path(__file__).resolve().parents[1]
errors=[]
def check(ok,reason):
    if not ok:errors.append(reason)
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
files=[ROOT/p for p in subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0') if p]
check(bool(files),'No tracked source files')
for p in files:
    rel=p.relative_to(ROOT).as_posix()
    if p.is_symlink():
        check(not Path(p.readlink()).is_absolute() and p.resolve().is_relative_to(ROOT) and p.exists(),f'Nonportable symlink: {rel}')
        continue
    check(p.is_file(),f'Missing tracked file: {rel}')
    if not p.is_file():continue
    check(p.stat().st_size<100*1024*1024,f'GitHub file-size limit: {rel}')
    check(not any(x in p.relative_to(ROOT).parts for x in ['.local','.cache','.venv','node_modules','__pycache__']),f'Local-only path tracked: {rel}')
    check(p.suffix.lower() not in ['.dmp','.sav','.srm','.sfc','.smc','.gba','.nds','.z64','.n64','.v64','.cdc','.elf','.o','.a'],f'Private/runtime binary tracked: {rel}')
    if p.suffix in ['.md','.py','.c','.h','.inc','.js','.json','.sh','.yml'] and 'vendor/' not in rel and 'versions/' not in rel:
        text=p.read_text(errors='replace')
        check(not re.search(r'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',text),f'Possible secret: {rel}')
for p in [ROOT/'README.md',* (ROOT/'docs').glob('*.md'),ROOT/'CONTRIBUTING.md',ROOT/'LICENSE.md']:
    if p not in files:continue
    for target in re.findall(r'\]\(([^)]+)\)|\bsrc="([^"]+)"',p.read_text()):
        url=next(t for t in target if t).strip('<>')
        if re.match(r'^(https?://|#|mailto:)',url):continue
        url=unquote(url.split('#')[0].split('?')[0])
        check((p.parent/url).exists(),f'Broken local link {p.relative_to(ROOT)} -> {url}')
cat=json.loads((ROOT/'catalog/forge-store.json').read_text())
check((ROOT/'3ds/romfs/catalog.json').read_bytes()==(ROOT/'catalog/forge-store.json').read_bytes(),'Bundled/maintained catalog differs')
def paths(value):
    if isinstance(value,str) and value.startswith('assets/'):check((ROOT/value).is_file(),f'Missing catalog asset: {value}')
    elif isinstance(value,dict):
        for v in value.values():paths(v)
    elif isinstance(value,list):
        for v in value:paths(v)
paths(cat)
media=ROOT/'assets/store-resources'; manifest=json.loads((media/'manifest.json').read_text())
check(manifest==json.loads((ROOT/'3ds/romfs/media.json').read_text()),'Bundled media manifest differs')
expected={f"{a['id']}/{v['id']}" for a in cat['apps'] for v in a.get('videos',[])}
check(set(manifest)==expected,'Catalog media entries differ')
for key,item in manifest.items():
    p=media/(key.replace('/','-')+'.fvid')
    check(p.exists() and p.stat().st_size==item['bytes'] and digest(p)==item['sha256'],f'Media checksum: {key}')
    check(item['url'].startswith('https://raw.githubusercontent.com/EstebanPdN/forje.store/'),'Unexpected media URL')
    check('/releases/' not in item['url'],'Media release URL is forbidden')
build=ROOT/'builds/3ds/manifest.json'
if build.exists():
    info=json.loads(build.read_text());check(not info['debug_dumps'] and not info['dump_upload'],'Public build has debug uploads/dumps')
    for name,item in info['files'].items():
        p=build.parent/name;check(p.exists() and p.stat().st_size==item['bytes'] and digest(p)==item['sha256'],f'Build checksum: {name}')
    for name,sha in info['source_sha256'].items():check(digest(ROOT/name)==sha,f'Build/source mismatch: {name}')
    check(digest(ROOT/'3ds/romfs/media.json')==info['media_manifest_sha256'],'Build/media mismatch')
else:check(False,'Public build manifest missing')
for e in errors:print('FAIL:',e,file=sys.stderr)
if errors:sys.exit(1)
print(f'PASS: {len(files)} tracked files; local links, catalog, {len(manifest)} resources, build hashes and source fingerprints')
