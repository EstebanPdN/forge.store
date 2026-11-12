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
    parser.add_argument('--web',action='store_true',help='Also render local web reader pages (markdown-it-py 3.0.0)')
    args = parser.parse_args()
    snapshot = json.loads(args.snapshot.read_text())
    captured = datetime.datetime.fromisoformat(snapshot['captured_at'].replace('Z', '+00:00'))
    stamp = int(captured.timestamp())
    required=[]
    repos={a.get('repo') for a in json.loads((ROOT/'catalog/forge-store.json').read_text())['apps'] if a.get('repo')}
    repos.add('EstebanPdN/forge.store')
    for repo in repos:
        required += [(f'https://api.github.com/repos/{repo}'+suffix,ACCEPTS[1] if suffix=='/readme' else ACCEPTS[0])
                     for suffix in ('','/readme','/releases?per_page=12','/commits?per_page=10')]
    present={(x['url'],x['accept']) for x in snapshot['responses']}
    missing=set(required)-present
    if missing:raise ValueError('Curated reader snapshot is incomplete: '+str(sorted(missing)))
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
    if args.web:
        from markdown_it import MarkdownIt
        md=MarkdownIt('commonmark',{'html':True}).enable('table')
        pages={}
        for item in snapshot['responses']:
            path=item['url'].removeprefix('https://api.github.com')
            raw=item['accept']==ACCEPTS[1]
            data=md.render(item['body']) if raw else json.loads(item['body'])
            if '/releases?' in path:
                for release in data:
                    release['body_html']=md.render(release.get('body') or '')
            pages[('html:' if raw else 'json:')+path]=data
        (manifest.parent/'pages.json').write_text(json.dumps({'captured_at':snapshot['captured_at'],'pages':pages},ensure_ascii=False,separators=(',',':'))+'\n')
    print(f'{len(report)} bundled public responses, {sum(x["bytes"] for x in report)/1048576:.2f} MiB')

if __name__ == '__main__':
    main()
