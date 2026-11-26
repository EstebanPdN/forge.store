#!/usr/bin/env python3
"""Exercise actual updater/cache code with HTTP fixtures and mocked 3DS AM services."""
from pathlib import Path
import tempfile,subprocess,threading,http.server,os,json,hashlib,struct,zlib,time
root=Path(__file__).resolve().parents[1]
state={'case':'current','calls':0,'etag':False}
current=__import__('re').search(r'STORE_VERSION "([^"]+)"',(root/'source/store_version.h').read_text())[1]
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  state['calls']+=1;case=state['case']
  if '/cache/' in self.path:
   if case=='reset' and state['calls']==1:self.connection.shutdown(2);self.connection.close();return
   if case=='offline':self.send_error(503);return
   if self.headers.get('If-None-Match')=='"fixture-v1"' and case=='unchanged':
    state['etag']=True;self.send_response(304);self.end_headers();return
   body=b'changed' if case=='changed' else b'cached payload';self.send_response(200);self.send_header('ETag','"fixture-v1"');self.end_headers();self.wfile.write(body);return
  version=current if case=='current' else '1.3' if case=='older' else '1.10'
  tag='v'+version
  title_version=(1 << 10) | (10 << 4)
  payload=(b'3DSX'+b'x'*4092) if case.startswith('hb') else struct.pack('<QH',0 if case=='wrong-title' else 0x000400000F0F5E00,0 if case=='wrong-cia-version' else title_version)+b'x'*4086
  if case=='hb-bad':payload=b'BAD!'+payload[4:]
  if self.path.endswith(('update.json','releases/latest')):
   files={k:{'bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest()} for k in ['forge-store.cia','forge-store.3dsx']}
   if case=='hash':files['forge-store.cia']['sha256']='0'*64
   if case=='bad-hex':files['forge-store.cia']['sha256']='z'*64
   if case=='large-size':files['forge-store.cia']['bytes']=97*1024*1024
   if case=='missing-file':del files['forge-store.cia']
   if case=='size':files['forge-store.cia']['bytes']=-1
   if self.path.endswith('releases/latest'):
    if case=='rate-limit':self.send_error(403);return
    if case=='bad-version':tag='v1.invalid'
    assets=[{'name':f'forge-store-{tag}.'+k.rsplit('.',1)[1],'size':v['bytes'],'digest':'sha256:'+v['sha256']} for k,v in files.items()]
    body=json.dumps({'tag_name':tag,'draft':False,'prerelease':case=='prerelease','assets':assets}).encode()
   else:
    body=json.dumps({'version':version,'files':files}).encode()
   if case=='malformed':body=b'{'
   if case=='huge':body=b'x'*18000
  else:
   ext='3dsx' if case.startswith('hb') else 'cia'
   if self.path!=f'/EstebanPdN/forge.store/releases/download/{tag}/forge-store-{tag}.{ext}':
    self.send_error(404);return
   body=payload[:-5] if case=='truncated' else payload
  self.send_response(200);self.end_headers();self.wfile.write(body)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
with tempfile.TemporaryDirectory(prefix='forge-store-network-') as temp:
 p=Path(temp);(p/'mbedtls').mkdir();(p/'sdmc:/3ds/forge-store').mkdir(parents=True)
 (p/'platform.h').write_text((root/'tests/update-host/platform.h').read_text())
 (p/'mbedtls/sha256.h').write_text('''#include <CommonCrypto/CommonDigest.h>
typedef CC_SHA256_CTX mbedtls_sha256_context;
static void mbedtls_sha256_init(mbedtls_sha256_context*c){}
static void mbedtls_sha256_free(mbedtls_sha256_context*c){}
static int mbedtls_sha256_starts_ret(mbedtls_sha256_context*c,int x){CC_SHA256_Init(c);return 0;}
static int mbedtls_sha256_update_ret(mbedtls_sha256_context*c,const unsigned char*p,size_t n){CC_SHA256_Update(c,p,n);return 0;}
static int mbedtls_sha256_finish_ret(mbedtls_sha256_context*c,unsigned char*p){CC_SHA256_Final(p,c);return 0;}
''')
 for n in ['store_version.h','store_update_policy.h','forge_mode.h','http_cache.h','reader_cache.h','reader_cache.c','assets.h']:(p/n).write_text((root/'source'/n).read_text())
 types=(root/'source/store_update.h').read_text().replace('#include "common.h"','#include "platform.h"');(p/'store_update.h').write_text(types)
 src=(root/'source/store_update.c').read_text().replace('#include "network.h"','').replace('#include "hbl_launch.h"','')
 src+='\n'+(root/'tests/update-host/decision-main.inc').read_text()
 (p/'updater.c').write_text(src)
 flags=['clang','-g','-O1','-fsanitize=address,undefined','-Wno-deprecated-declarations','-I'+temp,'-I/opt/homebrew/include','-L/opt/homebrew/lib','-ljansson','-lcurl','-lz','-lpthread']
 subprocess.run(flags+[str(p/'updater.c'),'-o',str(p/'updater')],check=True)
 env={**os.environ,'TEST_PORT':str(server.server_port)}
 cases=[('current',0,1),('older',0,1),('malformed',0,2),('huge',0,2),('size',0,2),('large-size',0,2),('bad-hex',0,2),('missing-file',0,2),('deadline',0,2),('cancelled',0,2),('hash',0,6),('truncated',0,6),('wrong-title',0,6),('valid',3,6),('valid',1,6),('valid',2,6),('valid',0,5),('valid',4,6),('hb-valid',0,5),('hb-rename',0,5),('hb-keep',0,5),('hb-bad',0,6),('hb-missing',0,6),('decline',0,8),('cancel-wait',0,8),('bad-version',0,2),('prerelease',0,2),('wrong-cia-version',0,6),('rate-limit',0,5)]
 for case,fault,want in cases:
  state.update(case=case,calls=0);subprocess.run([str(p/'updater'),case,str(fault),str(want)],cwd=p,env=env,check=True)
  if case in ['decline','cancel-wait']:assert state['calls']==1,(case,state)
  if case=='rate-limit':assert state['calls']==3,(case,state)
 (p/'romfs:').mkdir()
 personal=p/'romfs:/personal-assets.fme'
 personal.write_bytes(b'fixture')
 for case in ('valid','hb-valid'):
  state.update(case=case,calls=0)
  subprocess.run([str(p/'updater'),case,'0','1'],cwd=p,env=env,check=True)
  assert state['calls']==0,(case,state)
 personal.unlink()
 public_code_catalog=p/'romfs:/forge-code-assets.fme'
 public_code_catalog.write_bytes(b'fixture')
 state.update(case='current',calls=0)
 subprocess.run([str(p/'updater'),'current','0','1'],cwd=p,env=env,check=True)
 assert state['calls']==1,state
 public_code_catalog.unlink()
 cache=(root/'source/http_cache.c').read_text().replace('#include "network.h"','#include "platform.h"')
 cache+=r'''
int main(int argc,char**argv){http_cache_init();curl_global_init(CURL_GLOBAL_DEFAULT);char e[128];size_t n;char*b=http_cache_get("https://fixture/cache/test","*/*",1024,atoi(argv[1]),NULL,NULL,&n,e,sizeof e);assert(b&&!strcmp(b,argv[2])&&n==strlen(b));free(b);
assert(http_cache_fresh("https://fixture/cache/test","*/*",3600));
char path[256],meta[272];http_cache_path("https://fixture/cache/test","*/*",path,sizeof path);snprintf(meta,sizeof meta,"%s.meta",path);save_metadata(meta,"\"fixture-v1\"","",time(NULL)-7200);assert(!http_cache_fresh("https://fixture/cache/test","*/*",3600));save_metadata(meta,"\"fixture-v1\"","",time(NULL));
curl_global_cleanup();return 0;}
'''
 (p/'cache.c').write_text(cache);subprocess.run(flags+[str(p/'cache.c'),str(p/'reader_cache.c'),str(root/'source/assets.c'),'-DASSETS_HOST_TEST','-o',str(p/'cache')],check=True)
 for case,refresh,expected,calls in [('reset',1,'cached payload',2),('fresh',0,'cached payload',0),('unchanged',1,'cached payload',1),('offline',1,'cached payload',2),('changed',1,'changed',1)]:
  state.update(case=case,calls=0,etag=False);subprocess.run([str(p/'cache'),str(refresh),expected],cwd=p,env=env,check=True);assert state['calls']==calls,(case,state);assert case!='unchanged' or state['etag']
 # Public RomFS snapshots must open without network, while a newer SD response
 # wins and corrupt snapshots still take the normal bounded HTTP path.
 bundled=(root/'source/http_cache.c').read_text().replace('#include "network.h"','#include "platform.h"')
 bundled+=r'''
int main(int argc,char**argv){
 http_cache_init();curl_global_init(CURL_GLOBAL_DEFAULT);char error[128];size_t size;
 const char *url=argc>3?argv[3]:"https://fixture/cache/bundle";
 assert(http_cache_bundled(url,"*/*"));
 char *body=http_cache_get(url,"*/*",1024,atoi(argv[1]),NULL,NULL,&size,error,sizeof error);
 assert(body&&size==strlen(argv[2])&&!strcmp(body,argv[2]));free(body);
 curl_global_cleanup();return 0;
}
'''
 (p/'bundled.c').write_text(bundled);subprocess.run(flags+[str(p/'bundled.c'),str(p/'reader_cache.c'),str(root/'source/assets.c'),'-DASSETS_HOST_TEST','-o',str(p/'bundled')],check=True)
 key=hashlib.sha256(b'https://fixture/cache/bundle*/*').hexdigest()
 bundle=p/'romfs:/reader-data'/key;bundle.parent.mkdir()
 body=b'bundled payload';packed=struct.pack('<4sQII',b'FGH2',int(time.time()),len(body),zlib.crc32(body))+body
 bundle.write_bytes(packed);disk=p/'sdmc:/3ds/forge-store/http-v1'/key
 def check_bundle(case,refresh,want,calls):
  state.update(case=case,calls=0);subprocess.run([str(p/'bundled'),str(refresh),want],cwd=p,env=env,check=True);assert state['calls']==calls,(case,state)
 check_bundle('offline',0,'bundled payload',0)
 check_bundle('offline',1,'bundled payload',2)
 check_bundle('changed',1,'changed',1)
 check_bundle('offline',0,'changed',0)
 disk.write_bytes(b'broken SD cache');check_bundle('offline',0,'bundled payload',0)
 bundle.write_bytes(packed[:-1]+bytes([packed[-1]^1]));check_bundle('fresh',0,'cached payload',1)
 print('PASS: bundled pages require no HTTP; newer SD wins; corrupt SD/ROMFS and offline refresh recover.')
 owned='https://api.github.com/repos/EstebanPdN/dusklight-3ds/readme'
 owned_key=hashlib.sha256((owned+'*/*').encode()).hexdigest()
 (p/'romfs:/reader-data'/owned_key).write_bytes(packed)
 old=b'old incompatible SD snapshot'
 (p/'sdmc:/3ds/forge-store/http-v1'/owned_key).write_bytes(struct.pack('<4sQII',b'FGH2',int(time.time()),len(old),zlib.crc32(old))+old)
 for refresh in (0,1):
  state.update(case='offline',calls=0)
  subprocess.run([str(p/'bundled'),str(refresh),'bundled payload',owned],cwd=p,env=env,check=True)
  assert state['calls']==0
 print('PASS: owned README ignores stale SD and refresh requests; zero network calls.')
 concurrency=(root/'source/http_cache.c').read_text().replace('#include "network.h"','#include "platform.h"')
 concurrency+=r'''
static void *cache_reader(void *unused){
    for(int i=0;i<6;i++){
        char error[128];size_t n;
        char *body=http_cache_get("https://fixture/cache/concurrent","*/*",1024,i%2,NULL,NULL,&n,error,sizeof error);
        assert(body&&n==14&&!strcmp(body,"cached payload"));free(body);
    }
    return NULL;
}
int main(void){
    http_cache_init();curl_global_init(CURL_GLOBAL_DEFAULT);
    pthread_t threads[4];for(int i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,cache_reader,NULL));
    for(int i=0;i<4;i++)pthread_join(threads[i],NULL);
    curl_global_cleanup();return 0;
}
'''
 (p/'concurrent.c').write_text(concurrency)
 tsan=[f.replace('-fsanitize=address,undefined','-fsanitize=thread') for f in flags]
 subprocess.run(tsan+[str(p/'updater.c'),'-o',str(p/'updater-tsan')],check=True)
 for case,want in [('valid',5),('decline',8),('cancel-wait',8)]:
  state.update(case=case,calls=0);subprocess.run([str(p/'updater-tsan'),case,'0',str(want)],cwd=p,env=env,check=True)
 print('PASS: accept, reject and shutdown while awaiting consent; ThreadSanitizer.')
 subprocess.run(tsan+[str(p/'concurrent.c'),str(p/'reader_cache.c'),str(root/'source/assets.c'),'-DASSETS_HOST_TEST','-o',str(p/'concurrent')],check=True)
 state.update(case='fresh',calls=0)
 subprocess.run([str(p/'concurrent')],cwd=p,env=env,check=True)
 print('PASS: four concurrent cache readers/writers, 24 requests; ThreadSanitizer.')
 print(f'PASS: {len(cases)} updater scenarios; cache hit, conditional 304, changed payload, reset retry and offline fallback; ASan/UBSan. CIA services mocked.')
server.shutdown()
manifest=json.loads((root.parent/'assets/reader-data/manifest.json').read_text())
for item in manifest['responses']:
 blob=(root.parent/item['file']).read_bytes();magic,stamp,length,crc=struct.unpack('<4sQII',blob[:20])
 assert magic==b'FGH2' and 0<length<=1024*1024 and len(blob)==20+length and zlib.crc32(blob[20:])==crc
 assert hashlib.sha256(blob).hexdigest()==item['sha256']
 assert Path(item['file']).name==hashlib.sha256((item['url']+item['accept']).encode()).hexdigest()
print(f'PASS: {len(manifest["responses"])} bundled responses: cache keys, lengths, CRCs and SHA-256.')
