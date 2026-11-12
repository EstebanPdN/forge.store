#!/usr/bin/env python3
"""Export the bundled public snapshot; optionally refresh named curated repos.

No credentials are used or stored. Explicit --refresh owner/repo fetches the four
public endpoints for that repository. Output stays local until reviewed and packed.
Regular native builds never require the network or the author's private files.
"""
import argparse,json,struct,urllib.request,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
JSON='application/vnd.github+json';RAW='application/vnd.github.raw+json'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'.local/reader-authoring')
    parser.add_argument('--refresh',action='append',default=[],metavar='OWNER/REPO')
    args=parser.parse_args()
    catalog=json.loads((ROOT/'catalog/forge-store.json').read_text())
    repos={a['repo'] for a in catalog['apps'] if a.get('repo')}
    repos.update(('EstebanPdN/forge.store','EstebanPdN/EstebanPdN'))
    if not set(args.refresh)<=repos:parser.error('--refresh accepts only curated repositories and the store/profile')
    manifest=json.loads((ROOT/'assets/reader-data/manifest.json').read_text())
    responses={}
    for row in manifest['responses']:
        data=(ROOT/row['file']).read_bytes();magic,stamp,size,crc=struct.unpack_from('<4sQII',data)
        assert magic==b'FGH2' and len(data)==size+20 and zlib.crc32(data[20:])==crc
        responses[(row['url'],row['accept'])]={'url':row['url'],'accept':row['accept'],'body':data[20:].decode()}
    def fetch(url,accept):
        request=urllib.request.Request(url,headers={'Accept':accept,'User-Agent':'forge.store public snapshot authoring'})
        with urllib.request.urlopen(request,timeout=15) as response:body=response.read(1024*1024+1)
        if not body or len(body)>1024*1024:raise ValueError('Invalid response length: '+url)
        return body.decode('utf-8')
    for repo in args.refresh:
        url='https://api.github.com/repos/'+repo
        meta=fetch(url,JSON);value=json.loads(meta)
        if value.get('private') is not False or 'permissions' in value:raise ValueError('Public metadata required: '+repo)
        fresh=[{'url':url,'accept':JSON,'body':meta}]
        for suffix,accept in (('/readme',RAW),('/releases?per_page=12',JSON),('/commits?per_page=10',JSON)):
            fresh.append({'url':url+suffix,'accept':accept,'body':fetch(url+suffix,accept)})
        for row in fresh:responses[(row['url'],row['accept'])]=row
    readmes={}
    for repo in repos:
        base='https://api.github.com/repos/'+repo
        raw=responses.get((base+'/readme',RAW));meta=responses.get((base,JSON))
        if raw:
            branch=json.loads(meta['body']).get('default_branch','main') if meta else 'main'
            readmes[repo]={'content':raw['body'],'display_url':f'https://github.com/{repo}/blob/{branch}/README.md'}
    # Preserve the oldest capture date when refreshing only part of a snapshot.
    # Fresh dynamic responses will also be conditionally checked on the console.
    snapshot={'captured_at':manifest['captured_at'],'responses':list(responses.values())}
    args.output.mkdir(parents=True,exist_ok=True)
    for name,value in (('snapshot.json',snapshot),('readmes.json',readmes)):
        path=args.output/name;temp=path.with_suffix('.tmp')
        temp.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n');temp.replace(path)
    print(f'Exported {len(responses)} public responses; refreshed {len(args.refresh)} repositories to {args.output}')

if __name__=='__main__':main()
