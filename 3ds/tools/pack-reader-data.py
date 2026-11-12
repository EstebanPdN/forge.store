#!/usr/bin/env python3
"""Pack reviewed public GitHub responses for instant, offline native reader use.

Authoring input: {captured_at: ISO timestamp, responses: [{url, accept, body}]}.
body is a UTF-8 string. Fetching/reviewing that public snapshot is an explicit
authoring operation; regular builds use the committed, CRC-checked FGH2 files.
No access tokens, request headers or private repository fields are retained.
"""
import argparse
import datetime
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ACCEPTS = ('application/vnd.github+json', 'application/vnd.github.raw+json')
ENDPOINT = re.compile(r'https://api\.github\.com/(?:repos/[\w.-]+/[\w.-]+(?:/readme|/releases\?per_page=12|/commits\?per_page=10)?|users/[\w.-]+(?:/repos\?sort=pushed&per_page=40)?)\Z')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    args = parser.parse_args()
    snapshot = json.loads(args.snapshot.read_text())
    captured = datetime.datetime.fromisoformat(snapshot['captured_at'].replace('Z', '+00:00'))
    stamp = int(captured.timestamp())
    out = ROOT / '3ds/romfs/reader-data'
    out.mkdir(exist_ok=True)
    report = []
    names = set()
    for item in sorted(snapshot['responses'], key=lambda x: (x['url'], x['accept'])):
        url, accept, body = item['url'], item['accept'], item['body'].encode('utf-8')
        if not ENDPOINT.fullmatch(url) or accept not in ACCEPTS or not 0 < len(body) <= 1024 * 1024:
            raise ValueError(f'Unsupported public snapshot: {url}')
        if accept == ACCEPTS[0]:
            data = json.loads(body)
            if isinstance(data, dict) and (data.get('private') or 'permissions' in data):
                raise ValueError(f'Refuse private/permission metadata: {url}')
        key = hashlib.sha256((url + accept).encode()).hexdigest()
        if key in names:
            raise ValueError(f'Duplicate response: {url}')
        names.add(key)
        payload = struct.pack('<4sQII', b'FGH2', stamp, len(body), zlib.crc32(body)) + body
        target = out / key
        target.write_bytes(payload)
        report.append({'url': url, 'accept': accept, 'file': target.relative_to(ROOT).as_posix(),
                       'bytes': len(body), 'sha256': hashlib.sha256(payload).hexdigest()})
    # Remove only prior generated responses superseded by this complete snapshot.
    for path in out.iterdir():
        if re.fullmatch(r'[0-9a-f]{64}', path.name) and path.name not in names:
            path.unlink()
    manifest = ROOT / 'assets/reader-data/manifest.json'
    manifest.parent.mkdir(exist_ok=True)
    manifest.write_text(json.dumps({'format': 'FGH2', 'captured_at': snapshot['captured_at'],
                                    'responses': report}, ensure_ascii=False, indent=2) + '\n')
    print(f'{len(report)} bundled public responses, {sum(x["bytes"] for x in report)/1048576:.2f} MiB')

if __name__ == '__main__':
    main()
