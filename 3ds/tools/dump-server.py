#!/usr/bin/env python3
"""Receives the debug dumps a test build sends after SELECT (see source/debug_dump.c).

Usage: python3 3ds/tools/dump-server.py [port]      (default 8644)

The console PUTs /<token>/<dump>/<file>; files land in 3ds/build/dumps/<dump>/. The
token lives in 3ds/build/dump-token.txt (created on first run) and is baked into the
build through FORGE_DUMP_URL, so only that build can write here. Names are checked
against a fixed list and sizes are capped; nothing outside 3ds/build/dumps is touched.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import secrets
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/dumps'
TOKEN_FILE = ROOT / 'build/dump-token.txt'
FILES = {'info.txt', 'top.bmp', 'top-right.bmp', 'bottom.bmp', 'vram.bin', 'ram-image.bin', 'ram-heap.bin', 'ram-linear.bin'}
LIMIT = 160 * 1024 * 1024


def token():
    if not TOKEN_FILE.exists():
        TOKEN_FILE.parent.mkdir(parents=True, exist_ok=True)
        TOKEN_FILE.write_text(secrets.token_hex(12) + '\n')
    return TOKEN_FILE.read_text().strip()


class Handler(BaseHTTPRequestHandler):
    def do_PUT(self):
        parts = self.path.strip('/').split('/')
        length = int(self.headers.get('Content-Length') or -1)
        if (len(parts) != 3 or not secrets.compare_digest(parts[0], token()) or not re.fullmatch(r'\d{4}-\d{6}', parts[1])
                or parts[2] not in FILES or not 0 <= length <= LIMIT):
            self.send_error(403)
            return
        folder = OUT / parts[1]
        folder.mkdir(parents=True, exist_ok=True)
        left = length
        with open(folder / parts[2], 'wb') as f:
            while left:
                chunk = self.rfile.read(min(left, 1 << 16))
                if not chunk:
                    break
                f.write(chunk)
                left -= len(chunk)
        self.send_response(201 if not left else 400)
        self.send_header('Content-Length', '0')
        self.end_headers()
        print(f'{parts[1]}/{parts[2]}: {length - left} bytes', flush=True)

    def log_message(self, *args):
        pass


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8644
    print(f'dump server on :{port}, token {token()}, writing to {OUT}', flush=True)
    ThreadingHTTPServer(('', port), Handler).serve_forever()
