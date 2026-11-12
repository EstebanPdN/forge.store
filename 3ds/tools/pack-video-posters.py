#!/usr/bin/env python3
"""Rebuild gallery bottom posters from verified local FVIDs, without recoding videos."""
from pathlib import Path
import io, json, runpy, struct, sys, subprocess
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
sys.argv = [sys.argv[0], str(ROOT)]
pack = runpy.run_path(str(ROOT / '3ds/tools/pack.py'))
report = []
for app in json.loads((ROOT/'catalog/forge-store.json').read_text())['apps']:
    videos = app.get('videos', [])
    if not videos: continue
    posters = []
    for v in videos:
        frame = None
        with (ROOT/'3ds/media'/f"{app['id']}-{v['id']}.fvid").open('rb') as f:
            magic, fps, count, bottom, audio = struct.unpack('<4sIIII', f.read(20))
            assert magic == b'FVID' and count > 0
            index = min(count - 1, round(fps * .5))
            f.seek(20 + index * 4); offset, = struct.unpack('<I', f.read(4)); f.seek(offset)
            top_n, bottom_n, pcm_n = struct.unpack('<III', f.read(12))
            if bottom and bottom_n:
                # Chromium displays the original MP4 at 176x132 with nearest
                # sampling. Avoid a second resize of the lossy 320px FVID.
                raw = subprocess.check_output(['ffmpeg','-v','error','-ss','0.5','-i',str(ROOT/v['bottom']),'-frames:v','1','-vf','scale=176:132:flags=neighbor','-f','image2pipe','-vcodec','png','-threads','1','-'])
                frame = Image.open(io.BytesIO(raw)).convert('RGBA')
        if frame is None:
            frame = Image.open(ROOT/v['poster']).convert('RGBA').resize((176,99),Image.Resampling.BILINEAR)
        assert frame.size in [(176,132),(176,99)]
        posters.append(frame); report.append({'app':app['id'],'id':v['id'],'bottom':bool(bottom),'size':list(frame.size)})
    pack['write_sheet'](ROOT/'3ds/romfs/video'/f"{app['id']}-bottom-posters.ftx", posters, pack['RGB565'])
(ROOT/'3ds/build/gen/video-posters-v2.json').write_text(json.dumps(report,indent=2)+'\n')
print(f'{len(report)} gallery posters generated')
