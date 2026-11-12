#!/usr/bin/env python3
"""Compare raw native pixels; never rescales, aligns or recolors inputs.
Reports exact equality and <=8/16/24 channel tolerance, plus block SSIM.
Usage: compare-ui.py --native /tmp/forge-az/run --web docs/ui-v2-review --out docs/native-v2-parity/round-1
"""
import argparse,hashlib,json,shutil,re
from pathlib import Path
from PIL import Image,ImageDraw
import numpy as np
ap=argparse.ArgumentParser();ap.add_argument('--native',type=Path,required=True);ap.add_argument('--web',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
rows=[]
log=(a.native/'log.txt').read_text() if (a.native/'log.txt').exists() else ''
native_states={m[1]:m[2] for m in re.finditer(r'^shot (\S+) (.*?)(?=^shot |\Z)',log,re.M|re.S)}
def state_check(name):
 p=a.web/(name+'.state.json');block=native_states.get(name);issues=[]
 if not p.exists() or not block:return False,['Capture state metadata unavailable']
 w=json.loads(p.read_text());n=re.search(r'^state app=(\S+) tag=(\S+) dark=(\d) lang=(\S+) format=(\S+) channel=(\S+)',block,re.M);c=re.search(r'^capture scene=(.*?) id=(\S+) tab=(-?\d+) media=(-?\d+) playing=(\d) paused=(\d+)',block,re.M)
 if not n or not c:return False,['Native capture state unavailable']
 reader=re.search(r'reader_loading=(\d).*reader_error=([^\n]*)',block)
 if w.get('scene')=='GitHubScene' and reader and (reader[1]!='0' or reader[2]):issues.append('Native reader not ready: '+(reader[2] or 'loading'))
 assets=re.search(r'^assets visible=(\d+) resident=(\d+)',block,re.M)
 if assets and assets[1]!=assets[2]:issues.append('Visible native images still unavailable: '+assets[2]+'/'+assets[1])
 if w.get('app') and w['app']!=n[1]:issues.append('Different app: '+n[1]+' / '+w['app'])
 if w.get('releaseTag') and w['releaseTag']!=n[2]:issues.append('Different release: '+n[2]+' / '+w['releaseTag'])
 if w.get('renderTheme') and (w['renderTheme']=='dark')!=bool(int(n[3])):issues.append('Different theme')
 lang=w.get('settings',{}).get('language',w.get('settings',{}).get('lang'))
 if lang and lang!=n[4]:issues.append('Different language: '+n[4]+' / '+lang)
 view=re.search(r'^view kind=(\S+) scroll=([\d.-]+) video_time=([\d.-]+)',block,re.M)
 if view:
  expected={'HomeScene':'home','DetailScene':'detail','IntroScene':'intro','InstallScene':'install','SettingsScene':'settings','SearchScene':'search','SDScene':'sd','DownloadsScene':'downloads','GitHubScene':'github'}.get(w.get('scene'))
  if expected and expected!=view[1]:issues.append('Different scene: '+view[1]+' / '+expected)
  if w.get('media',{}).get('paused') and w['media'].get('time') is not None and abs(float(view[3])-w['media']['time'])>.051:issues.append('Different paused video timestamp')
 if w.get('tab'):
  tab={'info':0,'shots':1,'install':2,'news':3,'readme':0,'profile':0,'repos':1,'releases':1,'about':2}.get(w['tab'])
  if w.get('scene')=='GitHubScene' and w['tab']=='info':tab=2
  if tab is not None and int(c[3])!=tab:issues.append('Different tab')
 if w.get('query') is not None:
  query=re.search(r' query=(.*?) sd=',block)
  if not query or query[1]!=w['query']:issues.append('Different search query')
 if w.get('queue') is not None:
  queue=re.search(r'^queue focus=(\d+) scroll=([\d.-]+) rows=(.*)',block,re.M)
  mapping={'queued':0,'running':1,'done':2,'failed':3,'cancelled':4}
  expected=','.join(f"{j['app']}:{mapping[j['status']]}:{j['missing']}" for j in w['queue'])+(',' if w['queue'] else '')
  if not queue or queue[3]!=expected:issues.append('Different queue data/order')
  if queue and w.get('focus') is not None and int(queue[1])!=w['focus']:issues.append('Different queue selection')
 media=w.get('media',{})
 if 'playing' in media and bool(int(c[5]))!=media['playing']:issues.append('Different playback state')
 if media.get('playing') and 'paused' in media and bool(int(c[6]))!=media['paused']:issues.append('Different pause state')
 return not issues,issues
def ssim(x,y):
 x=x.astype(float);y=y.astype(float);h,w=x.shape[:2];h-=h%8;w-=w%8
 x=x[:h,:w].reshape(h//8,8,w//8,8,3).transpose(0,2,1,3,4).reshape(-1,64,3)
 y=y[:h,:w].reshape(h//8,8,w//8,8,3).transpose(0,2,1,3,4).reshape(-1,64,3)
 mx=x.mean(1);my=y.mean(1);vx=x.var(1);vy=y.var(1);cov=((x-mx[:,None])*(y-my[:,None])).mean(1)
 return float((((2*mx*my+6.5025)*(2*cov+58.5225))/((mx*mx+my*my+6.5025)*(vx+vy+58.5225))).mean())
for native in sorted(a.native.glob('*.png')):
 valid,issues=state_check(native.stem)
 if any((a.web/(native.stem+'-'+screen+'.png')).exists() for screen in ['top','bottom']):
  raw=a.out/'raw';raw.mkdir(exist_ok=True);shutil.copyfile(native,raw/native.name)
  meta=a.web/(native.stem+'.state.json')
  if meta.exists():shutil.copyfile(meta,raw/meta.name)
 for screen,box in [('top',(0,0,400,240)),('bottom',(40,240,360,480))]:
  web=a.web/(native.stem+'-'+screen+'.png')
  if not web.exists():continue
  ref=Image.open(web).convert('RGB');got=Image.open(native).convert('RGB').crop(box)
  assert ref.size==got.size,(web,ref.size,got.size)
  r=np.asarray(ref,dtype=np.int16);n=np.asarray(got,dtype=np.int16);delta=np.abs(r-n);d=delta.max(2)
  metrics={f'pixels_within_{t}':round(float((d<=t).mean()*100),3) for t in [0,8,16,24]}
  metrics.update(mae=round(float(delta.mean()),3),ssim=round(ssim(r,n),4))
  key=native.stem+'-'+screen
  shutil.copyfile(web,a.out/(key+'-web.png'));got.save(a.out/(key+'-native.png'))
  heat=np.zeros_like(r,dtype=np.uint8);heat[:,:,0]=np.minimum(255,d*4);heat[:,:,1]=np.minimum(180,d);Image.fromarray(heat).save(a.out/(key+'-diff.png'))
  sheet=Image.new('RGB',(ref.width*3,264),'#16181b');sheet.paste(ref,(0,24));sheet.paste(got,(ref.width,24));sheet.paste(Image.fromarray(heat),(ref.width*2,24));draw=ImageDraw.Draw(sheet)
  draw.text((8,6),'WEB',fill='white');draw.text((ref.width+8,6),'3DS',fill='white');draw.text((ref.width*2+8,6),f'DIFF / <=16: {metrics["pixels_within_16"]}%',fill='white');sheet.save(a.out/(key+'-pair.png'))
  rows.append(dict(scenario=native.stem,screen=screen,state_valid=valid,state_issues=issues,web_sha256=hashlib.sha256(web.read_bytes()).hexdigest(),native_sha256=hashlib.sha256(native.read_bytes()).hexdigest(),native_screen_sha256=hashlib.sha256((a.out/(key+'-native.png')).read_bytes()).hexdigest(),**metrics))
report=dict(method='Unaligned native pixels. A pixel matches at tolerance N only when all three RGB channel differences are <=N. 16/255 is a color tolerance, not a claim of byte identity. Full-screen scores include background; inspect pair and diff images. Data/clock/motion mismatches are not masked.',rows=rows)
(a.out/'metrics.json').write_text(json.dumps(report,indent=2)+'\n')
for name in ['log.txt','build-snapshot.json','audio-check.json','music-signal.json']:
 if (a.native/name).exists():shutil.copyfile(a.native/name,a.out/name)
for row in sorted(rows,key=lambda r:r['pixels_within_16']):print(f'{row["scenario"]:20s} {row["screen"]:6s} {row["pixels_within_16"]:6.2f}% SSIM {row["ssim"]:.3f}'+('' if row['state_valid'] else ' INVALID: '+', '.join(row['state_issues'])))
