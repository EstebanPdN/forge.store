#!/usr/bin/env python3
"""Encode the prototype's local videos to a bounded-memory 3DS MJPEG/PCM stream.
FVID header: magic, fps, frame count, has-bottom, has-audio (u32 LE).
Each frame: top/bottom JPEG lengths + PCM byte count, then the three payloads.
Frame offsets follow the header for seeking. UI runs at 60 Hz; video at 20 fps.
"""
from pathlib import Path
import json,struct,subprocess,tempfile,sys,runpy,io
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
sys.argv=[sys.argv[0],str(ROOT)]
pack=runpy.run_path(str(ROOT/'3ds/tools/pack.py'))
OUT=ROOT/'3ds/media';OUT.mkdir(exist_ok=True)
POSTERS=ROOT/'3ds/romfs/video';POSTERS.mkdir(exist_ok=True)
FPS=20;SAMPLES=32000//FPS

def encode(src,dest,w,h,pixel=False):
    # JPEG/libjpeg uses the BT.601 matrix. Explicitly convert BT.709 MP4 input
    # instead of retaining its YUV matrix and decoding it as a different space.
    flags='neighbor' if pixel else 'bilinear'
    # Chromium image-rendering:pixelated selects the upper/left sample when
    # halving these 2× split-screen sources; swscale selects lower/right. Shift
    # in RGB (YUV420 would round the odd crop) to retain the same pixel phase.
    phase=''
    if pixel:
        dimensions=json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0','-show_entries','stream=width,height','-of','json',str(ROOT/src)]))['streams'][0]
        size=(dimensions['width'],dimensions['height'])
        if size==(w*2,h*2):
            phase='format=rgb24,pad=iw+2:ih+2:1:1,crop=iw-2:ih-2:0:0,'
        elif size!=(w,h):
            raise ValueError(f'Pixel-video sampling needs a reviewed 1×/2× source: {src}')
    filt=f'fps={FPS},{phase}scale={w}:{h}:force_original_aspect_ratio=decrease:flags={flags}:out_color_matrix=bt601:out_range=pc,pad={w}:{h}:(ow-iw)/2:(oh-ih)/2'
    subprocess.run(['ffmpeg','-v','error','-threads','2','-y','-i',str(ROOT/src),'-an','-vf',filt,'-colorspace','bt470bg','-color_range','pc','-q:v','5','-threads','2',str(dest/'%06d.jpg')],check=True)

for app in json.loads((ROOT/'catalog/forge-store.json').read_text())['apps']:
    vids=app.get('videos',[])
    if not vids: continue
    posters=[]
    for video in vids:
        name=f"{app['id']}-{video['id']}";out=OUT/(name+'.fvid')
        posters.append(Image.open(ROOT/video['poster']).convert('RGBA').resize((400,240)))
        if out.exists() and out.stat().st_mtime>max((ROOT/video.get('src',video.get('top'))).stat().st_mtime,Path(__file__).stat().st_mtime):continue
        with tempfile.TemporaryDirectory(prefix='forge-video-') as t:
            t=Path(t);top=t/'top';bot=t/'bot';top.mkdir();bot.mkdir()
            encode(video.get('src',video.get('top')),top,400,240,pixel=bool(video.get('top')))
            if video.get('bottom'):encode(video['bottom'],bot,320,240,pixel=True)
            audio=b''
            if video.get('audio'):
                audio=subprocess.check_output(['ffmpeg','-v','error','-i',str(ROOT/video['src']),'-vn','-ac','1','-ar','32000','-f','s16le','-'])
            frames=sorted(top.glob('*.jpg'));bottom=sorted(bot.glob('*.jpg'));offsets=[]
            staging=out.with_suffix('.fvid.tmp')
            with open(staging,'wb') as f:
                f.write(struct.pack('<4sIIII',b'FVID',FPS,len(frames),bool(bottom),bool(audio)))
                f.write(bytes(4*len(frames)))
                for i,p in enumerate(frames):
                    a=p.read_bytes();b=bottom[min(i,len(bottom)-1)].read_bytes() if bottom else b''
                    pcm=audio[i*SAMPLES*2:(i+1)*SAMPLES*2].ljust(SAMPLES*2,b'\0') if audio else b''
                    offsets.append(f.tell());f.write(struct.pack('<III',len(a),len(b),len(pcm)));f.write(a);f.write(b);f.write(pcm)
                f.seek(20);f.write(struct.pack('<'+'I'*len(offsets),*offsets))
            staging.replace(out)
            print(name,len(frames),'frames',round(out.stat().st_size/1048576,1),'MB',flush=True)
    pack['write_sheet'](POSTERS/(app['id']+'-posters.ftx'),posters,pack['RGB565'])

runpy.run_path(str(ROOT/'3ds/tools/media-manifest.py'))['generate']()
