#!/usr/bin/env python3
"""Bake public README images once, at the reader's 376×240 maximum.

Input is a JSON object keyed by owner/repo, each with content (Markdown) and
display_url. Fetching is an explicit authoring step, never part of a normal
build. Run with --readmes <snapshot.json> --sharp-module <installed sharp>.
Only public images are downloaded. Output includes URLs, dimensions and hashes.
"""
import argparse, concurrent.futures, hashlib, html, io, json, re, struct, subprocess, urllib.parse, urllib.request, zlib
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
def urls(repo, item):
    source=item.get('content','')
    branch=item.get('display_url','/blob/main/').split('/blob/')[-1].split('/')[0]
    found=re.findall(r'(?:src|srcset)=["\']([^"\']+)["\']',source)
    found+=re.findall(r'!\[[^\]]*\]\(([^\s)]+)',source)
    for raw in found:
        raw=html.unescape(raw)
        if 'qr' in raw.lower() or raw.startswith(('data:','//')): continue
        if raw.startswith('http://'):raw='https://'+raw[7:]
        url=raw if raw.startswith('https://') else f'https://raw.githubusercontent.com/{repo}/{branch}/{raw.removeprefix("./").lstrip("/")}'
        if url.startswith('https://github.com/') and '/blob/' in url:
            url=url.replace('https://github.com/','https://raw.githubusercontent.com/',1).replace('/blob/','/',1)
        # Badges stay live via the normal conditional HTTP cache.
        if urllib.parse.urlsplit(url).hostname in ('raw.githubusercontent.com','avatars.githubusercontent.com','user-images.githubusercontent.com','github.com','db.universal-team.net','i.imgur.com','xproger.info','img.youtube.com'):
            yield url
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--readmes',type=Path,required=True);ap.add_argument('--sharp-module',default='sharp')
    ap.add_argument('--include-community',action='store_true',help='Prepare all supported images for every curated README')
    ap.add_argument('--extra-urls',type=Path,help='JSON array of reviewed public avatar/image URLs')
    args=ap.parse_args()
    data=json.loads(args.readmes.read_text());selected=set()
    for repo,item in data.items():
        if repo=='EstebanPdN/EstebanPdN':continue # native profile has its own portrait/layout
        found=[u for u in dict.fromkeys(urls(repo,item)) if not re.search(r'(?:website|discord|patreon)[^/]*\.svg',u,re.I)]
        if repo.startswith('EstebanPdN/'):selected.update(found)
        elif args.include_community:selected.update(found)
    if args.extra_urls:selected.update(json.loads(args.extra_urls.read_text()))
    selected.add('https://avatars.githubusercontent.com/u/72305261?v=4')
    sources=sorted(selected)
    out=ROOT/'3ds/romfs/reader-art';out.mkdir(exist_ok=True)
    cache=ROOT/'3ds/build/reader-originals';cache.mkdir(exist_ok=True)
    def fetch(url):
        key=hashlib.sha256(url.encode()).hexdigest();path=cache/key
        try:
            if not path.exists():
                req=urllib.request.Request(url,headers={'User-Agent':'forge.store asset packer'})
                try:
                    with urllib.request.urlopen(req,timeout=25) as r: blob=r.read(16*1024*1024+1)
                except Exception:
                    # The author's legacy screenshot host currently serves HTTP
                    # only. Import once here; runtime uses the bundled digest.
                    if urllib.parse.urlsplit(url).hostname!='xproger.info':raise
                    req=urllib.request.Request(url.replace('https://','http://',1),headers={'User-Agent':'forge.store asset packer'})
                    with urllib.request.urlopen(req,timeout=25) as r:blob=r.read(16*1024*1024+1)
                if len(blob)>16*1024*1024: raise ValueError('Image exceeds authoring limit')
                path.write_bytes(blob)
            return url,path,None
        except Exception as e:return url,path,str(e)
    script="const fs=require('fs'),sharp=require(process.argv[1]);sharp(fs.readFileSync(process.argv[2]),{limitInputPixels:64000000}).resize(+process.argv[4],+process.argv[5],{fit:'inside',withoutEnlargement:true}).ensureAlpha().raw().toBuffer({resolveWithObject:true}).then(({data,info})=>{fs.writeFileSync(process.argv[3],data);process.stdout.write(JSON.stringify(info));});"
    report=[];fail=[];web_urls={}
    web_out=ROOT/'assets/reader-art';web_out.mkdir(exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for url,path,error in pool.map(fetch,sources):
            if error:fail.append({'url':url,'error':error});continue
            try:
                raw_path=cache/(path.name+'.rgba')
                input_path=path
                # Some older 3DS READMEs use BMP screenshots. Convert those on
                # the authoring machine; the console only receives ready pixels.
                with path.open('rb') as f:bitmap=f.read(2)==b'BM'
                if bitmap:
                    with Image.open(path) as im:
                        if im.width*im.height>64000000:raise ValueError('Bitmap exceeds authoring limit')
                        input_path=cache/(path.name+'.png');im.convert('RGBA').save(input_path)
                avatar=urllib.parse.urlsplit(url).hostname=='avatars.githubusercontent.com'
                p=subprocess.run(['node','-e',script,args.sharp_module,str(input_path),str(raw_path),'64' if avatar else '376','64' if avatar else '240'],check=True,capture_output=True,text=True)
                info=json.loads(p.stdout);raw=raw_path.read_bytes();w,h=info['width'],info['height'];z=zlib.compress(raw,9)
                payload=struct.pack('<4sHHIII',b'RIM1',w,h,len(raw),len(z),zlib.crc32(raw))+z
                target=out/(path.name+'.rimg');target.write_bytes(payload)
                web_path=web_out/(path.name+'.webp')
                Image.frombytes('RGBA',(w,h),raw).save(web_path,lossless=True)
                web_urls[url]=web_path.relative_to(ROOT).as_posix()
                report.append({'url':url,'file':target.relative_to(ROOT).as_posix(),'width':w,'height':h,'bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest(),'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
            except Exception as e:fail.append({'url':url,'error':str(e)})
    (ROOT/'assets/reader-art').mkdir(exist_ok=True)
    (ROOT/'assets/reader-art/manifest.json').write_text(json.dumps({'format':1,'images':report},ensure_ascii=False,indent=2)+'\n')
    wanted={Path(v['file']).name for v in report}
    for old in out.glob('*.rimg'):
        if old.name not in wanted:old.unlink()
    (web_out/'urls.json').write_text(json.dumps(web_urls,separators=(',',':'))+'\n')
    (ROOT/'3ds/build/reader-art-errors.json').write_text(json.dumps(fail,indent=2))
    print(f'{len(report)} bundled images, {sum(x["bytes"] for x in report)/1048576:.2f} MiB; {len(fail)} unavailable (retain HTTP fallback)')
if __name__=='__main__':main()
