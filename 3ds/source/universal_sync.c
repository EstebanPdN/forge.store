#include "universal_sync.h"
#include "universal_convert.h"
#include "catalog.h"
#include "install.h"
#include "network.h"
#include "reader_cache.h"
#include <time.h>
#define LIMIT (4u*1024u*1024u)
#define CACHE DATA_DIR "/universal-v2.cache"
static Thread worker;
static bool started,ready,stop,paused,applied;
static json_t *result;
static char failure[160];
typedef struct {char *data;size_t length,capacity;} Buffer;
static bool stopped(void){return __atomic_load_n(&stop,__ATOMIC_ACQUIRE);}
static size_t receive(char *p,size_t size,size_t count,void *ctx){
    Buffer *b=ctx;if(stopped()||(size&&count>SIZE_MAX/size))return 0;size_t n=size*count;if(n>LIMIT-b->length)return 0;
    if(b->length+n+1>b->capacity){size_t cap=b->capacity?b->capacity*2:16384;if(cap<b->length+n+1)cap=b->length+n+1;if(cap>LIMIT+1)cap=LIMIT+1;char *q=realloc(b->data,cap);if(!q)return 0;b->data=q;b->capacity=cap;}
    memcpy(b->data+b->length,p,n);b->length+=n;b->data[b->length]=0;return n;
}
static int progress(void *ctx,curl_off_t a,curl_off_t b,curl_off_t c,curl_off_t d){
    (void)ctx;(void)a;(void)b;(void)c;(void)d;
    while(!stopped()&&__atomic_load_n(&paused,__ATOMIC_ACQUIRE))svcSleepThread(30000000);
    return stopped();
}
static json_t *fetch(const char *url){
    CURL *c=curl_easy_init();if(!c)return NULL;Buffer b={0};char error[CURL_ERROR_SIZE]={0};network_options(c);
    curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);curl_easy_setopt(c,CURLOPT_ERRORBUFFER,error);curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);
    CURLcode code=curl_easy_perform(c);json_t *j=NULL;
    if(code==CURLE_OK&&!stopped()&&b.data)j=json_loadb(b.data,b.length,JSON_REJECT_DUPLICATES,NULL);
    if(!j)snprintf(failure,sizeof failure,"%s",error[0]?error:"Invalid Universal-DB document");
    free(b.data);curl_easy_cleanup(c);return j;
}
static void run(void *ctx){
    (void)ctx;size_t n=0;time_t now=time(NULL);char *cached=reader_cache_load(CACHE,(uint64_t)now,21600,LIMIT,&n);
    if(cached){result=json_loadb(cached,n,JSON_REJECT_DUPLICATES,NULL);free(cached);if(!json_is_array(result)||!json_array_size(result)||json_array_size(result)>MAX_APPS){json_decref(result);result=NULL;}else goto done;}
    json_t *full=fetch("https://db.universal-team.net/data/full.json"),*store=NULL;
    if(full&&!stopped())store=fetch("https://db.universal-team.net/unistore/universal-db.unistore");
    if(full&&store&&!stopped())result=universal_convert(full,store);
    json_decref(full);json_decref(store);
    if(result&&!json_array_size(result)){json_decref(result);result=NULL;snprintf(failure,sizeof failure,"Empty Universal-DB; keeping bundled catalog");}
    if(result&&!stopped()){char *body=json_dumps(result,JSON_COMPACT);if(body){reader_cache_save(CACHE,(uint64_t)now,body,strlen(body),LIMIT);free(body);}}
done:__atomic_store_n(&ready,true,__ATOMIC_RELEASE);
}
bool universal_sync_pump(bool may_apply){
    bool installing=install_active()!=NULL;__atomic_store_n(&paused,installing,__ATOMIC_RELEASE);
    if(!started&&S.universal&&!installing){started=true;s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);worker=threadCreate(run,NULL,64*1024,priority+2,-2,false);if(!worker)snprintf(failure,sizeof failure,"Universal-DB worker unavailable");}
    if(!may_apply||installing||!__atomic_load_n(&ready,__ATOMIC_ACQUIRE))return false;
    if(worker){if(threadJoin(worker,0) != 0)return false;threadFree(worker);worker=NULL;}
    bool changed=result&&catalog_apply_universal(result);applied|=changed;json_decref(result);result=NULL;__atomic_store_n(&ready,false,__ATOMIC_RELEASE);
    if(failure[0])printf("Universal-DB: %s\n",failure);
    return changed;
}
void universal_sync_stop(void){__atomic_store_n(&stop,true,__ATOMIC_RELEASE);}
void universal_sync_exit(void){universal_sync_stop();if(!thread_reap(&worker))return;json_decref(result);result=NULL;}
const char *universal_sync_status(void){return applied?"applied":worker?"loading":started?"bundled":"idle";}
