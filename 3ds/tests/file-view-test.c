#define FILE_VIEW_HOST_TEST
#include "../source/file_view.c"
#undef stat
#undef calloc
#include <limits.h>
#include <fcntl.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);abort();}}while(0)
static float now=10;
static void put(const char *name,const char *data){FILE*f=fopen(name,"wb");CHECK(f);CHECK(fwrite(data,1,strlen(data),f)==strlen(data));CHECK(!fclose(f));}
static void settle(void){for(int i=0;i<5000;i++){file_view_pump(now);if(!worker&&file_view_completed()==requested)return;usleep(1000);}CHECK(!"worker timeout");}
static void wait_entered(void){for(int i=0;i<5000;i++){if(__atomic_load_n(&test_entered,__ATOMIC_ACQUIRE))return;usleep(1000);}CHECK(!"probe timeout");}
int main(void){
    test_main=pthread_self();char temp[]="/tmp/forge-file-view-XXXXXX";CHECK(mkdtemp(temp));CHECK(!chdir(temp));
    put("full","hello");put("empty","");CHECK(!mkdir("folder",0700));
    CHECK(!file_view_watch(NULL,false).token);CHECK(!file_view_watch("",false).token);
    char huge[1025];memset(huge,'x',1024);huge[1024]=0;CHECK(!file_view_watch(huge,false).token);
    CHECK(!file_view_exists((FileView){65535,1}));CHECK(!file_view_nonempty((FileView){0}));
    char name[16]="full";FileView full=file_view_watch(name,true);strcpy(name,"missing");
    FileView duplicate=file_view_watch("full",false);CHECK(full.slot==duplicate.slot&&full.token==duplicate.token);
    FileView empty=file_view_watch("empty",true),dir=file_view_watch("folder",true),missing=file_view_watch("missing",false);
    CHECK(!file_view_exists(full));settle();
    CHECK(file_view_exists(full)&&file_view_nonempty(full));CHECK(file_view_exists(empty)&&!file_view_nonempty(empty));
    CHECK(file_view_exists(dir)&&!file_view_nonempty(dir));CHECK(!file_view_exists(missing));
    unsigned calls=__atomic_load_n(&test_stat_calls,__ATOMIC_RELAXED);
    for(int i=0;i<10000;i++){CHECK(file_view_exists(full));CHECK(file_view_nonempty(full));}
    CHECK(calls==__atomic_load_n(&test_stat_calls,__ATOMIC_RELAXED));
    put("full","");CHECK(file_view_nonempty(full));unsigned ticket=file_view_refresh();settle();
    CHECK(file_view_completed()>=ticket&&!file_view_nonempty(full));
    put("full","new");file_view_pump(now+2.99f);CHECK(!worker&&!file_view_nonempty(full));
    now+=3.01f;file_view_pump(now);settle();CHECK(file_view_nonempty(full));
    // Forced refresh cannot complete using a snapshot launched before the ticket.
    __atomic_store_n(&test_hold,true,__ATOMIC_RELEASE);__atomic_store_n(&test_entered,false,__ATOMIC_RELEASE);
    file_view_refresh();file_view_pump(now);wait_entered();ticket=file_view_refresh();
    CHECK(file_view_completed()<ticket);FileView later=file_view_watch("later",true);put("later","x");
    __atomic_store_n(&test_hold,false,__ATOMIC_RELEASE);settle();CHECK(file_view_completed()>=ticket&&file_view_nonempty(later));
    // Fill the bounded registry, then recycle a slot while the old worker owns
    // its snapshot. Its result must never be applied to the replacement token.
    for(unsigned i=0;i<LIMIT;i++){char path[40];snprintf(path,sizeof path,"absent-%u",i);file_view_watch(path,false);}
    CHECK(!file_view_watch("overflow",false).token);
    __atomic_store_n(&test_hold,true,__ATOMIC_RELEASE);__atomic_store_n(&test_entered,false,__ATOMIC_RELEASE);
    file_view_refresh();file_view_pump(now);wait_entered();file_view_plan_begin();
    FileView replacement=file_view_watch("replacement",false);CHECK(replacement.slot==missing.slot&&replacement.token!=missing.token);
    CHECK(!file_view_exists(missing)&&!file_view_nonempty(missing));CHECK(!file_view_exists(replacement));put("replacement","yes");
    __atomic_store_n(&test_hold,false,__ATOMIC_RELEASE);settle();CHECK(file_view_nonempty(replacement));
    CHECK(!file_view_exists(missing));CHECK(file_view_nonempty(full));
    // OOM and thread failure are retried at most once per second, even when
    // refresh generations remain pending. Neither publishes a false completion.
    ticket=file_view_refresh();test_fail_alloc=true;unsigned allocs=test_alloc_calls;
    file_view_pump(now);CHECK(!worker&&test_alloc_calls==allocs+1&&file_view_completed()<ticket);
    for(int i=0;i<1000;i++)file_view_pump(now+.5f);CHECK(test_alloc_calls==allocs+1);
    test_fail_alloc=false;now+=1.01f;test_fail_thread=true;unsigned workers=test_workers;
    file_view_pump(now);CHECK(!worker&&test_workers==workers+1&&file_view_completed()<ticket);
    for(int i=0;i<1000;i++)file_view_pump(now+.5f);CHECK(test_workers==workers+1);
    test_fail_thread=false;now+=1.01f;settle();CHECK(file_view_completed()>=ticket);
    // Rewatching an inactive nonpermanent path schedules a fresh snapshot.
    file_view_plan_begin();calls=__atomic_load_n(&test_stat_calls,__ATOMIC_RELAXED);
    file_view_watch("replacement",false);settle();CHECK(__atomic_load_n(&test_stat_calls,__ATOMIC_RELAXED)>calls);
    // Shutdown joins the running reader before freeing snapshots/paths.
    file_view_refresh();file_view_pump(now);file_view_exit();CHECK(!worker&&!job);
    CHECK(!file_view_exists(full));
    CHECK(!unlink("full"));CHECK(!unlink("empty"));CHECK(!unlink("later"));CHECK(!unlink("replacement"));CHECK(!rmdir("folder"));
    CHECK(!chdir("/tmp"));CHECK(!rmdir(temp));
    printf("FileView: %u assertions passed; real filesystem, forced refresh, TTL, stale handles, worker snapshot recycling, OOM/thread retry, shutdown.\n",checks);
}
