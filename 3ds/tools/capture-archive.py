#!/usr/bin/env python3
"""Opt-in localhost receiver for parity PNGs; never writes to Downloads.

Run with --out docs/native-v2-parity/web-reference-current, then configure the
parity-web helper with http://127.0.0.1:8643/capture. Only this project's preview
origin and fixed native PNG dimensions are accepted; no arbitrary file paths.
"""
import argparse, base64, json, re, struct
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--port', type=int, default=8643)
args = ap.parse_args()
out = args.out.resolve()
if not out.is_relative_to(ROOT): raise SystemExit('Output must stay inside the project')
out.mkdir(parents=True, exist_ok=True)

class Receiver(BaseHTTPRequestHandler):
    def allowed(self):
        return self.headers.get('Origin') in ('http://localhost:8642', 'http://127.0.0.1:8642')
    def reply(self, code, value):
        self.send_response(code)
        if self.allowed(): self.send_header('Access-Control-Allow-Origin', self.headers['Origin'])
        self.send_header('Content-Type', 'application/json')
        self.end_headers(); self.wfile.write(json.dumps(value).encode())
    def do_OPTIONS(self):
        if not self.allowed(): return self.reply(403, {'error':'Origin rejected'})
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', self.headers['Origin'])
        self.send_header('Access-Control-Allow-Methods', 'POST')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()
    def do_POST(self):
        if self.path != '/capture' or not self.allowed(): return self.reply(403, {'error':'Request rejected'})
        try:
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size <= 8*1024*1024: raise ValueError('Invalid capture size')
            item = json.loads(self.rfile.read(size))
            name = item['name']
            if not isinstance(name,str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,120}',name): raise ValueError('Invalid name')
            files = {}
            for screen,width in [('top',400),('bottom',320)]:
                encoded = item[screen]
                if not encoded.startswith('data:image/png;base64,'): raise ValueError('PNG required')
                data = base64.b64decode(encoded.split(',',1)[1], validate=True)
                if data[:8] != b'\x89PNG\r\n\x1a\n' or len(data)<33 or struct.unpack('>II',data[16:24]) != (width,240): raise ValueError('Native dimensions required')
                files[name+'-'+screen+'.png'] = data
            if not isinstance(item['state'],dict): raise ValueError('State required')
            files[name+'.state.json'] = (json.dumps(item['state'],indent=2)+'\n').encode()
            for filename,data in files.items():
                target = out/filename
                if target.is_symlink(): raise ValueError('Symlink output rejected')
                temp = target.with_name('.'+filename+'.tmp'); temp.write_bytes(data); temp.replace(target)
            self.reply(200, {'saved':list(files)})
        except (ValueError,KeyError,TypeError) as error: self.reply(400,{'error':str(error)})

print(f'Capture archive: http://127.0.0.1:{args.port}/capture -> {out}',flush=True)
HTTPServer(('127.0.0.1',args.port),Receiver).serve_forever()
