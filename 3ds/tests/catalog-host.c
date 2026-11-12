// Actual catalog implementation, real Jansson and host files, SDK/failure fixtures only.
#include "catalog.h"
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

char rom_find_root[64] = "sdmc:";
static int fault, rename_calls, assertions;
static bool defer_thread, busy;
static void (*queued_fn)(void *);
static void *queued_arg;
enum { NONE, WRITE, FLUSH, SYNC, CLOSE, VALIDATE, ROTATE, PROMOTE, RESTORE, BACKUP, SERIALIZE };
#define CHECK(x) do { assertions++; if (!(x)) {fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); abort();} } while (0)

static size_t test_write(const void *p, size_t z, size_t n, FILE *f) {
    return fwrite(p,z,fault==WRITE && n ? n-1 : n,f);
}
static int test_flush(FILE *f) { int r=fflush(f); return fault==FLUSH ? -1 : r; }
static int test_sync(int fd) { return fault==SYNC ? -1 : fsync(fd); }
static int test_close(FILE *f) { int r=fclose(f); return fault==CLOSE ? -1 : r; }
static json_t *test_load(const char *p, size_t flags, json_error_t *e) {
    if (fault==VALIDATE && strstr(p,".tmp")) return json_object();
    return json_load_file(p,flags,e);
}
static char *test_dumps(const json_t *j,size_t flags) { return fault==SERIALIZE ? NULL : json_dumps(j,flags); }
static int test_rename(const char *a,const char *b) {
    rename_calls++;
    if ((fault==ROTATE && rename_calls==1) || (fault==PROMOTE && rename_calls==2) ||
        (fault==RESTORE && rename_calls>=2)) {errno=EIO;return -1;}
    // Model FAT rename: an existing destination cannot be atomically overwritten.
    struct stat st;
    if (!stat(b,&st)) {errno=EEXIST;return -1;}
    return rename(a,b);
}
static int test_remove(const char *p) {
    if (fault==BACKUP && strstr(p,".bak")) {errno=EACCES;return -1;}
    return remove(p);
}
#define fwrite test_write
#define fflush test_flush
#define fsync test_sync
#define fclose test_close
#define json_load_file test_load
#define json_dumps test_dumps
#define rename test_rename
#define remove test_remove
#include "../source/catalog.c"
#undef fwrite
#undef fflush
#undef fsync
#undef fclose
#undef json_load_file
#undef json_dumps
#undef rename
#undef remove

float NOW,DT; bool IS_NEW3DS;
bool remote_image_request(const char *url){(void)url;return false;}
bool remote_image_ready(const char *url){(void)url;return false;}
void LightLock_Lock(LightLock *l) { assert(!*l);*l=1; }
void LightLock_Unlock(LightLock *l) { assert(*l);*l=0; }
void CFGU_GetSystemLanguage(u8 *l) {*l=CFG_LANGUAGE_EN;}
void svcGetThreadPriority(s32 *p,int h) {*p=0x30;}
void svcSleepThread(int64_t ns) {if(queued_fn){void (*f)(void*)=queued_fn;queued_fn=NULL;f(queued_arg);}}
void *threadCreate(void (*fn)(void *),void *arg,size_t stack,int prio,int affinity,bool detached) {
    if(!defer_thread)return NULL;
    assert(!queued_fn);queued_fn=fn;queued_arg=arg;return (void*)1;
}
bool install_busy(App *a) {return busy;}
bool sheet_load_now(Sheet *s,const char *p) {return false;}
void sheet_request(Sheet *s,const char *p) {s->state=SHEET_QUEUED;}
void sheet_free(Sheet *s) {s->state=SHEET_EMPTY;}

static json_t *J(const char *s) {json_t *j=json_loads(s,JSON_DECODE_ANY,NULL);CHECK(j);return j;}
static void put(const char *p,const char *s) {FILE *f=fopen(p,"wb");CHECK(f);CHECK(fwrite(s,1,strlen(s),f)==strlen(s));CHECK(!fclose(f));}
static const char *str(json_t *j,const char *k) {return json_string_value(json_object_get(j,k));}
static void same(const char *a,const char *b) {CHECK(a && b && !strcmp(a,b));}
static void version(App *a,const char *s) {
    same(app_installed_version(a),s);
    json_t *disk=load_saved_object(DATA_DIR "/installed.json");
    same(str(json_object_get(disk,a->id),"version"),s);json_decref(disk);
}

static void persistence(void) {
    App a={.id="fixture",.j=J("{\"requires\":[]}")};
    installed_load();
    json_t *files=J("[\"sdmc:/3ds/test.3dsx\"]"),*dirs=J("[\"sdmc:/3ds/owned\"]"),*hash=J("{\"sdmc:/3ds/test.3dsx\":\"old\"}");
    CHECK(installed_commit_manifest(&a,"v1",false,false,files,dirs,hash));version(&a,"v1");
    CHECK(files->refcount==1 && dirs->refcount==1 && hash->refcount==1);
    CHECK(json_array_append_new(files,json_string("sdmc:/3ds/new.cia"))==0);
    CHECK(json_object_set_new(hash,"sdmc:/3ds/test.3dsx",json_string("new"))==0);
    CHECK(json_object_set_new(hash,"title:0004000001234567",json_string("title"))==0);
    CHECK(installed_commit_manifest(&a,"v2",true,true,files,dirs,hash));version(&a,"v2");
    CHECK(installed_commit(&a,"v3",false,false,NULL,NULL));version(&a,"v3");
    json_t *r=installed_record(&a);
    CHECK(json_is_true(json_object_get(r,"cia")));
    CHECK(json_array_size(json_object_get(r,"files"))==2);
    CHECK(json_array_size(json_object_get(r,"dirs"))==1);
    CHECK(json_array_size(json_object_get(r,"formats"))==2);
    same(str(r,"format"),"3dsx");same(str(r,"channel"),"stable");
    CHECK(strlen(str(r,"date"))==10);
    same(str(json_object_get(r,"hashes"),"sdmc:/3ds/test.3dsx"),"new");
    same(str(json_object_get(r,"hashes"),"title:0004000001234567"),"title");
    json_t *before=json_deep_copy(INSTALLED);
    for(int f=WRITE;f<=SERIALIZE;f++) {
        fault=f;rename_calls=0;
        CHECK(!installed_commit(&a,"broken",false,false,files,dirs));
        CHECK(json_equal(before,INSTALLED));
        fault=NONE;version(&a,"v3");
        installed_load();version(&a,"v3");
        // Recover a missing primary left by simultaneous promotion+restore failure.
        CHECK(installed_commit(&a,"v3",false,false,NULL,NULL));
    }
    json_decref(before);
    fault=SYNC;CHECK(!installed_forget(&a));fault=NONE;version(&a,"v3");
    CHECK(installed_forget(&a));CHECK(!installed_record(&a));installed_load();CHECK(!installed_record(&a));
    CHECK(installed_forget(&a)); // idempotent and still durably saved
    CHECK(installed_commit(&a,"v4",false,false,files,dirs));
    // A queued older serialized snapshot cannot overwrite a synchronous commit.
    defer_thread=true;save_json(INSTALLED,DATA_DIR "/installed.json");
    CHECK(installed_commit(&a,"v5",false,false,NULL,NULL));
    saves_flush();defer_thread=false;version(&a,"v5");CHECK(!save_pending);
    put(DATA_DIR "/installed.json","{");installed_load();version(&a,"v4");
    CHECK(installed_commit(&a,"v6",false,false,NULL,NULL));version(&a,"v6");
    put(DATA_DIR "/installed.json.tmp","{\"fixture\":{\"version\":\"uncommitted\"}}");
    installed_load();version(&a,"v6");
    CHECK(!installed_commit(&a,"",false,false,NULL,NULL));
    CHECK(!installed_commit_manifest(&a,"bad",false,false,NULL,NULL,files));
    installed_set(&a,"legacy",false,false,files);version(&a,"legacy");CHECK(files->refcount==1);
    installed_remove(&a);CHECK(!installed_record(&a));
    S.volume=.4f;settings_save();saves_flush();S.volume=.7f;settings_save();saves_flush();
    put(DATA_DIR "/settings.json","[]");settings_load();CHECK(fabsf(S.volume-.4f)<.001f);
    json_decref(files);json_decref(dirs);json_decref(hash);json_decref(a.j);
    puts("PASS persistence: write/flush/fsync/close/readback/rotation/promotion/restore/backup/serialization failures, recovery, hash merge, durable forget, stale writers, borrowed args");
}

static void requirements(void) {
    App a={.id="require",.j=J("{\"requires\":[{\"path\":\"/3ds/StarHaven/\",\"example\":\"Paper Mario (USA).z64\"}]}")};
    mkdir("sdmc:/3ds/StarHaven",0777);
    json_t *r=json_array_get(json_object_get(a.j,"requires"),0);
    CHECK(!app_requirement_met(&a,r));CHECK(app_missing_requirement(&a)==r);
    put("sdmc:/3ds/StarHaven/Paper Mario (USA).z64","");CHECK(!app_requirement_met(&a,r));
    put("sdmc:/3ds/StarHaven/Paper Mario (USA).z64","x");CHECK(app_requirement_met(&a,r));CHECK(!app_missing_requirement(&a));
    remove("sdmc:/3ds/StarHaven/Paper Mario (USA).z64");mkdir("sdmc:/3ds/StarHaven/Paper Mario (USA).z64",0777);CHECK(!app_requirement_met(&a,r));
    json_object_del(r,"example");CHECK(!app_requirement_met(&a,r));put("sdmc:/3ds/StarHaven/file.bin","x");CHECK(app_requirement_met(&a,r));
    json_object_set_new(r,"path",json_null());CHECK(!app_requirement_met(&a,r));CHECK(app_missing_requirement(&a)==r);
    json_object_set_new(r,"path",json_string("/3ds/../3ds/StarHaven/file.bin"));CHECK(!app_requirement_met(&a,r));
    char huge[1500];memset(huge,'a',sizeof huge);huge[0]='/';huge[1499]=0;json_object_set_new(r,"path",json_string(huge));CHECK(!app_requirement_met(&a,r));
    put("sdmc:/3ds/boot.3DSX","binary");put("sdmc:/3ds/zero.3dsx","");mkdir("sdmc:/3ds/dir.3dsx",0777);
    json_t *files=J("[\"title:0004000001234567\",\"/3ds/missing.3dsx\",\"/3ds/zero.3dsx\",\"/3ds/dir.3dsx\",\"/3ds/../3ds/boot.3DSX\",\"romfs:/boot.3dsx\",\"/3ds/boot.3DSX\"]");
    CHECK(installed_commit(&a,"v1",false,false,files,NULL));
    char out[128];CHECK(app_launch_path(&a,out,sizeof out));same(out,"sdmc:/3ds/boot.3DSX");
    CHECK(!app_launch_path(&a,out,4));CHECK(!*out);
    remove("sdmc:/3ds/boot.3DSX");CHECK(!app_launch_path(&a,out,sizeof out));CHECK(!*out);
    CHECK(path_exists("sdmc:/3ds/StarHaven"));CHECK(!path_exists(NULL));CHECK(!path_exists_cached(NULL));
    json_decref(files);json_decref(a.j);
    puts("PASS requirements/launch: example/default, pathless, empty/directory/regular files, traversal/device/length rejection, title marker exclusion, fresh disk changes");
}

static void search_and_releases(void) {
    CHECK(catalog_load());App *out[512];
    CHECK(catalog_search(" \t\xC2\xA0",out,512)==5);
    const char *ids[]={"alttp","minish","mk64","doom","starwing"};
    for(int i=0;i<5;i++)same(out[i]->id,ids[i]);
    CHECK(catalog_search("",out,2)==2);CHECK(catalog_search("x",out,-1)==0);CHECK(catalog_filter("all",out,-1)==0);
    CHECK(catalog_search("  ZÉLDA  ",out,512)>0);same(out[0]->id,"alttp");
    char *a=search_normalize("ÉCOLE JOÃO ПрИВЕТ が 한글"),*b=search_normalize(" école joão привет が 한글 ");same(a,b);free(a);free(b);
    App x={.id="release",.j=J("{\"releases\":{\"stable\":{\"tag\":\"v1\",\"date\":\"2026-01-01\"},\"experimental\":{\"tag\":\"v2\",\"date\":\"2026-02-01\"}},\"install\":{\"3dsx\":[{\"type\":\"downloadUrl\",\"size\":100},null,{}, {\"type\":\"downloadUrl\",\"size\":-5}]}}")};
    CHECK(app_has_experimental(&x));CHECK(app_release_experimental(&x,app_release(&x,true)));
    CHECK(app_download_size(&x,true,false)==100);
    json_t *rels=json_object_get(x.j,"releases"),*ex=json_object_get(rels,"experimental");
    json_object_del(ex,"date");CHECK(!app_has_experimental(&x));same(str(app_release(&x,true),"tag"),"v1");
    json_object_set_new(ex,"date",json_string("2026-01-01"));CHECK(!app_has_experimental(&x));
    json_object_set_new(ex,"date",json_string("2026-02-01"));
    CHECK(installed_commit(&x,"v2",true,false,NULL,NULL));S.experimental=false;CHECK(app_status(&x)==ST_INSTALLED);
    json_object_set_new(ex,"tag",json_string("v3"));CHECK(app_status(&x)==ST_UPDATE);
    busy=true;CHECK(app_status(&x)==ST_BUSY);busy=false;x.dev=true;CHECK(app_status(&x)==ST_DEV);x.dev=false;
    json_object_set_new(rels,"stable",json_null());CHECK(app_has_experimental(&x));same(str(app_release(&x,false),"tag"),"v3");
    json_object_set_new(ex,"tag",json_null());CHECK(!app_release(&x,true));CHECK(app_status(&x)==ST_INSTALLED);
    CHECK(!app_release(NULL,true));CHECK(!app_has_format(NULL,false));CHECK(!app_asset(NULL,NULL,NULL));
    json_decref(x.j);
    // Localized tagline, joined-field query boundaries, ranking (full-title prefix is score 2).
    NAPPS=2;APPS[0]=(App){.id="a",.title="Other",.updated="2026-02-01",.j=J("{\"fullTitle\":\"Zelda\",\"tagline\":{\"en\":\"Blue sky\",\"es\":\"Corazón único\"}}")};
    APPS[1]=(App){.id="b",.title="Zelda",.updated="2026-01-01",.j=J("{}")};
    S.lang=0;CHECK(catalog_search(" corazon UNICO ",out,2)==1);same(out[0]->id,"a");S.lang=1;CHECK(!catalog_search("corazon",out,2));
    CHECK(catalog_search("zelda",out,2)==2);same(out[0]->id,"b");
    CHECK(catalog_search("other zelda",out,2)==1);same(out[0]->id,"a");
    json_decref(APPS[0].j);json_decref(APPS[1].j);
    puts("PASS search/releases: real catalog suggestions, composed/decomposed Latin/Cyrillic/Japanese/Hangul, trim/localized tagline/ranking, invalid releases, actual channels/status, format fallback");
}

static void live_universal(void){
    CHECK(catalog_load());int original_total=NAPPS;catalog_universal(true);original_total=NAPPS;
    App *known=&APPS[BUILTIN_APPS];int icon=known->icon;const char *old_title=known->title;json_t *apps=json_array(),*update=json_deep_copy(known->j),*added=json_deep_copy(known->j);
    json_object_set_new(update,"title",json_string("Refreshed universal title"));json_array_append_new(apps,update);
    json_object_set_new(added,"id",json_string("udb-new-fixture"));json_object_set_new(added,"title",json_string("New live entry"));json_array_append_new(apps,added);
    json_t *bad=json_pack("[{}]");CHECK(!catalog_apply_universal(bad));json_decref(bad);
    bad=json_array();json_array_append(bad,update);json_array_append(bad,update);CHECK(!catalog_apply_universal(bad));json_decref(bad);
    CHECK(catalog_apply_universal(apps));CHECK(NAPPS==original_total+1);CHECK(known==&APPS[BUILTIN_APPS]&&known->icon==icon);same(known->title,"Refreshed universal title");CHECK(old_title&&*old_title);CHECK(!known->retired);CHECK(APPS[BUILTIN_APPS+1].retired);
    App *out[MAX_APPS];CHECK(catalog_filter("universal",out,MAX_APPS)==2);CHECK(catalog_search("New live entry",out,MAX_APPS)==1);same(out[0]->id,"udb-new-fixture");CHECK(out[0]->icon==-1);CHECK(!catalog_apply_universal(apps));
    catalog_universal(false);CHECK(NAPPS==BUILTIN_APPS);catalog_universal(true);CHECK(catalog_filter("universal",out,MAX_APPS)==2);json_decref(apps);
    puts("PASS Universal registration: stable app addresses/art indices, new IDs, retirement, search, toggling, malformed/duplicate generation rejection");
    catalog_icons_begin();for(int i=0;i<7;i++)catalog_request_icon(&APPS[BUILTIN_APPS+i*50]);catalog_icons_end();for(int i=0;i<7;i++)CHECK(universal_icons[i].state==SHEET_QUEUED);
    catalog_icons_begin();catalog_request_icon(&APPS[BUILTIN_APPS+150]);catalog_icons_end();for(int i=0;i<7;i++)CHECK(universal_icons[i].state==(i==3?SHEET_QUEUED:SHEET_EMPTY));
    puts("PASS visible icon working set: all seven sorted sheets coexist, leaving the grid releases six, no mid-batch eviction");
}
static void gallery_cache(void) {
 App a={.id="fixture",.shots=12};for(int k=0;k<3;k++)a.shot_index[k]=-1;
 for(int i=0;i<12;i++)a.shot_bottom[i]=(i%2)==0;
 app_request_gallery(&a,0,-1);CHECK(a.shot_thumbs.state==SHEET_QUEUED);
 for(int i=0;i<12;i++) {
  int previous=(i+11)%12;app_request_gallery(&a,i,previous);
  bool current=false,old=false;
  for(int k=0;k<3;k++){current|=a.shot_index[k]==i;old|=a.shot_index[k]==previous;CHECK(a.shot_index[k]>=0&&a.shot_index[k]<12);}
  CHECK(current&&old);
 }
 int warm[3];memcpy(warm,a.shot_index,sizeof warm);app_request_gallery(&a,-1,-1);CHECK(!memcmp(warm,a.shot_index,sizeof warm));
 bool ready=true;app_shot(&a,1,true,&ready);CHECK(!ready); // sparse bottom index is not shifted
 app_free_shots(&a);CHECK(a.shot_thumbs.state==SHEET_EMPTY);for(int k=0;k<3;k++)CHECK(a.shot_index[k]==-1);
 puts("PASS bounded gallery: current/previous/next, sparse bottoms, warm video return, cleanup");
}
int main(void) {persistence();requirements();search_and_releases();live_universal();gallery_cache();printf("PASS catalog production host tests: %d assertions (ASan + UBSan)\n",assertions);return 0;}
