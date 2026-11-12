#!/usr/bin/env python3
"""Verify the reviewed resource set and write the native download manifest.

Only assets/store-resources is distributable. URLs point into the source repository,
never a game release. hosting.json may pin the asset commit for immutable downloads.
"""
from pathlib import Path
import hashlib
import json
ROOT = Path(__file__).resolve().parents[2]
MEDIA = ROOT / 'assets/store-resources'
REPO = 'EstebanPdN/forge.store'

def generate():
    report = json.loads((MEDIA / 'report.json').read_text())
    reviewed = {item['key']: item for item in report['files']}
    hosting = json.loads((MEDIA / 'hosting.json').read_text()) if (MEDIA / 'hosting.json').exists() else {}
    manifest = {}
    for app in json.loads((ROOT / 'catalog/forge-store.json').read_text())['apps']:
        for video in app.get('videos', []):
            key = f"{app['id']}/{video['id']}"
            name = f"{app['id']}-{video['id']}.fvid"
            data = (MEDIA / name).read_bytes()
            sha = hashlib.sha256(data).hexdigest()
            expected = reviewed[key]
            if data[:4] != b'FVD2' or expected['sha256'] != sha or expected['bytes'] != len(data):
                raise ValueError(f'Resource changed since review: {key}')
            entry = hosting.get(key)
            if hosting and (not entry or entry['sha256'] != sha or entry['bytes'] != len(data)):
                raise ValueError(f'Hosting record changed: {key}')
            url = entry['url'] if entry else f'https://raw.githubusercontent.com/{REPO}/main/assets/store-resources/{name}'
            if not url.startswith(f'https://raw.githubusercontent.com/{REPO}/'):
                raise ValueError(f'Unexpected resource host: {key}')
            manifest[key] = {'url': url, 'sha256': sha, 'bytes': len(data)}
    if set(manifest) != set(reviewed):
        raise ValueError('Catalog and reviewed resources disagree')
    content = json.dumps(manifest, indent=2) + '\n'
    (MEDIA / 'manifest.json').write_text(content)
    (ROOT / '3ds/romfs/media.json').write_text(content)
    return manifest

if __name__ == '__main__':
    print('Remote previews:', len(generate()))
