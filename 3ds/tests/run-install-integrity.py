#!/usr/bin/env python3
"""Compiles production install.c; every sdmc: access is relative to a fresh temp dir."""
import hashlib, http.server, pathlib, ssl, subprocess, tempfile, threading
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='forge-install-test-') as temp:
    p=pathlib.Path(temp)
    header=(root/'source/install.h').read_text().replace('#include "catalog.h"','')
    (p/'install-types.h').write_text(header)
    (p/'malloc.h').write_text('#include <stdlib.h>\nstatic void *memalign(size_t a,size_t n){void*p=0;return posix_memalign(&p,a,n)?0:p;}\n')
    cmd=['clang','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-Wno-deprecated-declarations','-Wno-format','-I'+temp,'-I/opt/homebrew/include','-I/opt/homebrew/opt/libarchive/include',str(root/'tests/install-integrity.c'),'-L/opt/homebrew/lib','-L/opt/homebrew/opt/libarchive/lib','-ljansson','-larchive','-lcurl','-lz','-o',str(p/'test')]
    subprocess.run(cmd,check=True)
    (p/'integrity').mkdir()
    subprocess.run([str(p/'test')],cwd=p/'integrity',check=True)

    body = bytes(range(256)) * 128
    requests = []

    class InterruptOnce(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            first = self.path == '/1' and all(path != self.path for path, _ in requests)
            requests.append((self.path, self.headers.get('Range')))
            # Always answer 200, even to a Range request.
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body[:8192] if first else body)

    cert = p/'romfs:/cacert.pem'
    cert.parent.mkdir()
    key = p/'tls.key'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                    '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
                    '-keyout', str(key), '-out', str(cert)], check=True, stderr=subprocess.PIPE)
    with http.server.HTTPServer(('127.0.0.1', 0), InterruptOnce) as server:
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(cert, key)
        server.socket = tls.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            subprocess.run([str(p/'test'), f'https://localhost:{server.server_port}',
                            hashlib.sha256(body).hexdigest()], cwd=p, timeout=20, check=True)
        finally:
            server.shutdown()
            thread.join()
    assert requests == [('/0', None), ('/1', None), ('/1', 'bytes=8192-'), ('/1', None)], requests
    print('PASS: interrupted TLS transfer restarts when Range is ignored; verified sibling fetched once.')
