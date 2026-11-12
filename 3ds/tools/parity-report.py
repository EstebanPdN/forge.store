#!/usr/bin/env python3
"""Consolidate reviewed capture rounds without hiding failures or build versions."""
from pathlib import Path
import json, hashlib

ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'docs/native-v2-parity'
rounds=['locales-round-1','intro-round-2','requirements-round-2','sd-round-2',
        'universal-round-3-new','memory-player-old','installed-states-cache-old',
        'search-downloads-old-r2','final-suite-old-core','final-suite-old-reader',
        'final-suite-new-core','final-suite-new-reader','avatars-final-old','final-focus-new']
latest={};diagnostics=[]
binary=ROOT/'3ds/build/forge-store.3dsx'
current=hashlib.sha256(binary.read_bytes()).hexdigest()
for name in rounds:
    directory=BASE/name;file=directory/'metrics.json'
    if not file.exists():continue
    snapshot=json.loads((directory/'build-snapshot.json').read_text()) if (directory/'build-snapshot.json').exists() else {}
    model=('New 3DS' if snapshot['new3ds'] else 'Old 3DS') if 'new3ds' in snapshot else 'Unrecorded model'
    log=(directory/'log.txt').read_text() if (directory/'log.txt').exists() else ''
    if model=='Unrecorded model':
        if 'Old 3DS' in log:model='Old 3DS'
        elif 'New 3DS' in log:model='New 3DS'
    for row in json.loads(file.read_text())['rows']:
        key=row['scenario']+'-'+row['screen']
        entry={**row,'round':name,'model':model,'build_sha256':snapshot.get('image_sha256'),
               'current_binary':snapshot.get('image_sha256')==current,'capture_method':'NativePreview native SVG export',
               'files':{kind:str((directory/(key+'-'+kind+'.png')).relative_to(ROOT)) for kind in ['web','native','diff','pair']}}
        if not row.get('state_valid',False):
            entry['state_valid']=False
            if 'state_valid' not in row:entry['state_issues']=['Historical round without automated state validation']
            diagnostics.append(entry);continue
        latest[(model,row['scenario'],row['screen'])]=entry
rows=sorted(latest.values(),key=lambda r:(r['model'],r['scenario'],r['screen']))
compositor=[]
for name in ['compositor-samples-core','compositor-samples-downloads']:
    file=BASE/name/'metrics.json'
    if file.exists():compositor.extend({**r,'round':name} for r in json.loads(file.read_text())['rows'])
report={'status':'Work in progress; not global parity or hardware certification',
        'criterion':'Approximately 80–90% pixel similarity across equivalent scenarios, plus functional/audio parity',
        'method':'No alignment/resizing/masks/color fitting. RGB16 means each channel differs by at most 16/255; backgrounds count. Keep exact equality, MAE and SSIM alongside this score.',
        'current_binary_sha256':current,'screens':len(rows),'at_least_80':sum(r['pixels_within_16']>=80 for r in rows),
        'below_80':sum(r['pixels_within_16']<80 for r in rows),'invalid_diagnostics':diagnostics,
        'rows':rows,'compositor_diagnostics':compositor,
        'limits':['Different rounds may use different builds; every recorded build hash is retained.',
                  'Primary PNGs are native-size SVG exports from the web product. Actual compositor samples remain lower in several views and are reported separately.',
                  'Selected scenarios/languages/themes are covered; this is not every possible app/state/animation frame.',
                  'CIA services, launch, HOME/suspend and physical audio/performance still require actual consoles.']}
(BASE/'review-matrix.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['screens','at_least_80','below_80','current_binary_sha256']},indent=2))
for model in sorted({r['model'] for r in rows}):
    group=[r['pixels_within_16'] for r in rows if r['model']==model];print(model,len(group),min(group),max(group))
