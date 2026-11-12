#!/usr/bin/env python3
"""Check actual NDSP state from the trailer navigation run, not synthetic output."""
from pathlib import Path
import argparse,json,re,hashlib
ap=argparse.ArgumentParser();ap.add_argument('--run',type=Path,required=True);a=ap.parse_args();log=(a.run/'log.txt').read_text();shots={}
gain=None;video=None
for line in log.splitlines():
 m=re.search(r'audio volume=([\d.]+) gain=([\d.]+).*underruns=(\d+)',line)
 if m:gain={'volume':float(m[1]),'gain':float(m[2]),'underruns':int(m[3])};assert gain['underruns']==0
 m=re.search(r'video time=([\d.]+) paused=(\d) muted=(\d)',line)
 if m:video={'time':float(m[1]),'paused':bool(int(m[2])),'muted':bool(int(m[3]))}
 m=re.match(r'shot (audio-\S+) ',line)
 if m:shots[m[1]]={'audio':dict(gain),'video':dict(video) if video else None}
for name in ['playing','unmuted','paused','seek','resumed']:
 s=shots['audio-'+name];assert s['audio']['gain']<.001,(name,s)
 assert not s['video']['muted'],(name,s)
for name in ['muted','return']:
 s=shots['audio-'+name];assert abs(s['audio']['gain']-min(1,2*.85*s['audio']['volume']))<.003,(name,s)
assert shots['audio-muted']['video']['muted']
assert shots['audio-paused']['video']['paused'] and shots['audio-seek']['video']['paused']
assert abs(shots['audio-seek']['video']['time']-shots['audio-paused']['video']['time']-5)<.06
assert not shots['audio-playing']['video']['paused'] and not shots['audio-resumed']['video']['paused']
record=a.run/'recording.webm';assert record.is_file() and record.stat().st_size>0
report={'status':'pass','run':str(a.run),'checks':['Actual audible trailer playback','Music remains ducked during pause/seek as on the web','Mute and close restore min(1, 2 × 0.85 × master volume) mix gain','Paused seek advances exactly five seconds','Resume and zero music underruns'],'shots':shots,'recording_bytes':record.stat().st_size,'recording_sha256':hashlib.sha256(record.read_bytes()).hexdigest(),'limit':'Emulator NDSP state and recording; physical output not validated'}
(a.run/'audio-check.json').write_text(json.dumps(report,indent=2)+'\n');(Path(__file__).resolve().parents[1]/'build/gen/audio-v2-check.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: actual trailer controls, music duck/restore, paused +5 s seek, resume, recording and zero underruns')
