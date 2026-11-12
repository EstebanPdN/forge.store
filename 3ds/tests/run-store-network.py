#!/usr/bin/env python3
"""Exercise actual updater/cache code with HTTP fixtures and mocked 3DS AM services."""
from pathlib import Path
import tempfile,subprocess,threading,http.server,os,json,hashlib,struct
root=Path(__file__).resolve().parents[1]
state={'case':'current','calls':0,'etag':False}
seq=int(__import__('re').search(r'STORE_BUILD (\d+)',(root/'source/store_version.h').read_text())[1])
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
  payload=(b'3DSX'+b'x'*4092) if case.startswith('hb') else struct.pack('<Q',0 if case=='wrong-title' else 0x000400000F0F5E00)+b'x'*4088
  if case=='hb-bad':payload=b'BAD!'+payload[4:]
  if self.path.endswith('update.json'):
   files={k:{'bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest()} for k in ['forge-store.cia','forge-store.3dsx']}
   if case=='hash':files['forge-store.cia']['sha256']='0'*64
   if case=='bad-hex':files['forge-store.cia']['sha256']='z'*64
   if case=='large-size':files['forge-store.cia']['bytes']=97*1024*1024
   if case=='missing-file':del files['forge-store.cia']
   if case=='size':files['forge-store.cia']['bytes']=-1
   body=json.dumps({'store_build':seq if case=='current' else seq-1 if case=='older' else seq+1,'files':files}).encode()
   if case=='malformed':body=b'{'
   if case=='huge':body=b'x'*18000
  else:body=payload[:-5] if case=='truncated' else payload
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
 for n in ['store_version.h','store_update_policy.h','http_cache.h','reader_cache.h','reader_cache.c']:(p/n).write_text((root/'source'/n).read_text())
 types=(root/'source/store_update.h').read_text().replace('#include "common.h"','#include "platform.h"');(p/'store_update.h').write_text(types)
 src=(root/'source/store_update.c').read_text().replace('#include "network.h"','').replace('#include "hbl_launch.h"','')
 src+='''
int main(int argc,char**argv){curl_global_init(CURL_GLOBAL_DEFAULT);hb=!strncmp(argv[1],"hb",2);homebrew=hb;fault=atoi(argv[2]);strcpy(executable,"sdmc:/app.3dsx");FILE*f=fopen(executable,"wb");fwrite("3DSXold",1,7,f);fclose(f);if(!strcmp(argv[1],"hb-missing"))strcpy(executable,"sdmc:/missing.3dsx");if(!strcmp(argv[1],"deadline")){started_at=osGetTime()-5001;store_update_poll();assert(store_update_state()==UPDATE_OFFLINE);}if(!strcmp(argv[1],"cancelled"))cancelled=true;run(NULL);int want=atoi(argv[3]);assert(store_update_state()==want);if(want!=UPDATE_DONE)assert(finishes==0||fault==2);if(want==UPDATE_DONE&&!hb)assert(starts==1&&finishes==1&&writes>0);if(fault==1||fault==2)assert(cancels==1);if(!strcmp(argv[1],"wrong-title")||fault==3)assert(starts==0);if(hb){f=fopen("sdmc:/app.3dsx","rb");char b[8]={0};fread(b,1,7,f);fclose(f);assert(!memcmp(b,want==UPDATE_DONE?"3DSXxxx":"3DSXold",7));}if(hb&&want==UPDATE_DONE){f=fopen("sdmc:/app.forge-previous.3dsx","rb");assert(f);char backup[8]={0};assert(fread(backup,1,7,f)==7&&!memcmp(backup,"3DSXold",7));fclose(f);}assert(access(DATA_DIR "/store-update.part",F_OK));curl_global_cleanup();return 0;}
'''
 (p/'updater.c').write_text(src)
 flags=['clang','-g','-O1','-fsanitize=address,undefined','-Wno-deprecated-declarations','-I'+temp,'-I/opt/homebrew/include','-L/opt/homebrew/lib','-ljansson','-lcurl','-lz','-lpthread']
 subprocess.run(flags+[str(p/'updater.c'),'-o',str(p/'updater')],check=True)
 env={**os.environ,'TEST_PORT':str(server.server_port)}
 cases=[('current',0,1),('older',0,1),('malformed',0,2),('huge',0,2),('size',0,2),('large-size',0,2),('bad-hex',0,2),('missing-file',0,2),('deadline',0,2),('cancelled',0,2),('hash',0,6),('truncated',0,6),('wrong-title',0,6),('valid',3,6),('valid',1,6),('valid',2,6),('valid',0,5),('hb-valid',0,5),('hb-bad',0,6),('hb-missing',0,6)]
 for case,fault,want in cases:
  state.update(case=case,calls=0);subprocess.run([str(p/'updater'),case,str(fault),str(want)],cwd=p,env=env,check=True)
 cache=(root/'source/http_cache.c').read_text().replace('#include "network.h"','#include "platform.h"')
 cache+=r'''
int main(int argc,char**argv){http_cache_init();curl_global_init(CURL_GLOBAL_DEFAULT);char e[128];size_t n;char*b=http_cache_get("https://fixture/cache/test","*/*",1024,atoi(argv[1]),NULL,NULL,&n,e,sizeof e);assert(b&&!strcmp(b,argv[2])&&n==strlen(b));free(b);
assert(http_cache_fresh("https://fixture/cache/test","*/*",3600));
char path[256],meta[272];http_cache_path("https://fixture/cache/test","*/*",path,sizeof path);snprintf(meta,sizeof meta,"%s.meta",path);save_metadata(meta,"\"fixture-v1\"","",time(NULL)-7200);assert(!http_cache_fresh("https://fixture/cache/test","*/*",3600));save_metadata(meta,"\"fixture-v1\"","",time(NULL));
curl_global_cleanup();return 0;}
'''
 (p/'cache.c').write_text(cache);subprocess.run(flags+[str(p/'cache.c'),str(p/'reader_cache.c'),'-o',str(p/'cache')],check=True)
 for case,refresh,expected,calls in [('reset',1,'cached payload',2),('fresh',0,'cached payload',0),('unchanged',1,'cached payload',1),('offline',1,'cached payload',2),('changed',1,'changed',1)]:
  state.update(case=case,calls=0,etag=False);subprocess.run([str(p/'cache'),str(refresh),expected],cwd=p,env=env,check=True);assert state['calls']==calls,(case,state);assert case!='unchanged' or state['etag']
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
 subprocess.run(tsan+[str(p/'concurrent.c'),str(p/'reader_cache.c'),'-o',str(p/'concurrent')],check=True)
 state.update(case='fresh',calls=0)
 subprocess.run([str(p/'concurrent')],cwd=p,env=env,check=True)
 print('PASS: four concurrent cache readers/writers, 24 requests; ThreadSanitizer.')
 print(f'PASS: {len(cases)} updater scenarios; cache hit, conditional 304, changed payload, reset retry and offline fallback; ASan/UBSan. CIA services mocked.')
server.shutdown()
