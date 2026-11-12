#!/usr/bin/env python3
"""Local server for the prototype. Unlike `python3 -m http.server`, it answers
HTTP Range requests (needed to seek inside videos) and proxies the GitHub API.

GitHub proxy: /gh-api/<path> -> https://api.github.com/<path>. Answers are cached
on disk for an hour, and if a token is available (GITHUB_TOKEN, or `gh auth token`
from the GitHub CLI) requests are authenticated: 5,000 calls per hour instead of
60. The token stays in this process; the browser never sees it."""
import hashlib
import http.server
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
CACHE = os.path.join(ROOT, '.cache', 'gh-api')
TTL = 3600


def github_token():
    token = os.environ.get('GITHUB_TOKEN')
    if token:
        return token
    try:
        return subprocess.run(['gh', 'auth', 'token'], capture_output=True, text=True, timeout=5).stdout.strip() or None
    except (OSError, subprocess.SubprocessError):
        return None


TOKEN = github_token()


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def end_headers(self):
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()

    def do_GET(self):
        if self.path.startswith('/gh-api/'):
            return self.github()
        return super().do_GET()

    def github(self):
        path = self.path[len('/gh-api'):]
        accept = self.headers.get('Accept', 'application/vnd.github+json')
        os.makedirs(CACHE, exist_ok=True)
        key = os.path.join(CACHE, hashlib.sha1(f'{accept} {path}'.encode()).hexdigest() + '.json')
        cached = None
        if os.path.exists(key):
            with open(key) as f:
                cached = json.load(f)
            if time.time() - cached['at'] < TTL:
                return self.reply(cached['status'], cached['type'], cached['body'].encode(), 'hit')
        request = urllib.request.Request('https://api.github.com' + path, headers={'Accept': accept, 'User-Agent': 'forge-store-prototype'})
        if TOKEN:
            request.add_header('Authorization', f'Bearer {TOKEN}')
        try:
            with urllib.request.urlopen(request, timeout=20) as res:
                status, ctype, body = res.status, res.headers.get('Content-Type', 'application/json'), res.read()
        except urllib.error.HTTPError as error:
            status, ctype, body = error.code, error.headers.get('Content-Type', 'application/json'), error.read()
        except (urllib.error.URLError, TimeoutError):
            if cached:  # offline: serve the stale copy rather than nothing
                return self.reply(cached['status'], cached['type'], cached['body'].encode(), 'stale')
            return self.reply(502, 'application/json', b'{"message":"offline"}', 'miss')
        if status in (200, 404):
            with open(key, 'w') as f:
                json.dump({'at': time.time(), 'status': status, 'type': ctype, 'body': body.decode('utf-8', 'replace')}, f)
        elif cached:  # rate-limited or failing: keep showing the last good answer
            return self.reply(cached['status'], cached['type'], cached['body'].encode(), 'stale')
        self.reply(status, ctype, body, 'miss')

    def reply(self, status, ctype, body, cache):
        self.send_response(status)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('X-Forge-Cache', cache)
        self.end_headers()
        self.wfile.write(body)

    def send_head(self):
        match = re.fullmatch(r'bytes=(\d*)-(\d*)', self.headers.get('Range', ''))
        path = self.translate_path(self.path)
        if not match or not os.path.isfile(path):
            return super().send_head()
        size = os.path.getsize(path)
        start, end = match.groups()
        if start:
            start, end = int(start), min(int(end) if end else size - 1, size - 1)
        else:
            start, end = max(0, size - int(end or 0)), size - 1
        if start > end or start >= size:
            self.send_response(416)
            self.send_header('Content-Range', f'bytes */{size}')
            self.end_headers()
            return None
        f = open(path, 'rb')
        f.seek(start)
        self.remaining = end - start + 1
        self.send_response(206)
        self.send_header('Content-Type', self.guess_type(path))
        self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.send_header('Content-Length', str(self.remaining))
        self.end_headers()
        return f

    def copyfile(self, source, outputfile):
        remaining = getattr(self, 'remaining', None)
        if remaining is None:
            return super().copyfile(source, outputfile)
        while remaining > 0:
            chunk = source.read(min(65536, remaining))
            if not chunk:
                break
            outputfile.write(chunk)
            remaining -= len(chunk)
        self.remaining = None


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8642
    print(f'forge.store → http://localhost:{port}  (Ctrl+C para cerrar)')
    print('GitHub: ' + ('autenticado (5.000 consultas/hora)' if TOKEN else 'sin token (60 consultas/hora); usa GITHUB_TOKEN o `gh auth login`'))
    http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()
