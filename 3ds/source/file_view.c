#include "file_view.h"
#ifdef FILE_VIEW_HOST_TEST
#include "../tests/file_view_platform.h"
#else
#include "common.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define LIMIT 512
typedef struct {char *path;uint32_t token;bool permanent,wanted,known,exists,nonempty;} Entry;
typedef struct {char path[1024];uint16_t slot;uint32_t token;bool exists,nonempty;} Probe;
static Entry entries[LIMIT];
static Probe *job;
static unsigned count,requested=1,launched,completed,token;
static float next,retry_after;
static Thread worker;
static bool ready,stop;
static bool valid(FileView h){return h.slot<LIMIT&&h.token&&entries[h.slot].token==h.token;}
FileView file_view_watch(const char *path,bool permanent){
    FileView none={0};if(!path||!*path||strlen(path)>=sizeof job[0].path)return none;int slot=-1;
    for(int i=0;i<LIMIT;i++){if(entries[i].path&&!strcmp(entries[i].path,path)){if(!entries[i].permanent&&!entries[i].wanted)requested++;entries[i].permanent|=permanent;entries[i].wanted=true;return (FileView){i,entries[i].token};}if(slot<0&&!entries[i].path)slot=i;}
    if(slot<0)for(int i=0;i<LIMIT;i++)if(!entries[i].permanent&&!entries[i].wanted){slot=i;break;}
    if(slot<0)return none;
    char *copy=strdup(path);if(!copy)return none;
    free(entries[slot].path);if(!++token)token++;
    entries[slot]=(Entry){.path=copy,.token=token,.permanent=permanent,.wanted=true};requested++;return (FileView){slot,token};
}
void file_view_plan_begin(void){for(int i=0;i<LIMIT;i++)if(!entries[i].permanent)entries[i].wanted=false;}
bool file_view_exists(FileView h){return valid(h)&&entries[h.slot].known&&entries[h.slot].exists;}
bool file_view_nonempty(FileView h){return valid(h)&&entries[h.slot].known&&entries[h.slot].nonempty;}
unsigned file_view_refresh(void){return ++requested;}
unsigned file_view_completed(void){return completed;}
// Worker owns only this immutable snapshot. Registry/handles stay main-thread-only.
static void probe(void *unused){
    (void)unused;
    for(unsigned i=0;i<count&&!__atomic_load_n(&stop,__ATOMIC_ACQUIRE);i++){struct stat st;if(!stat(job[i].path,&st)){job[i].exists=true;job[i].nonempty=S_ISREG(st.st_mode)&&st.st_size>0;}}
    __atomic_store_n(&ready,true,__ATOMIC_RELEASE);
}
void file_view_pump(float now){
    if(worker){if(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE)||threadJoin(worker,0) != 0)return;threadFree(worker);worker=NULL;
        for(unsigned i=0;i<count;i++){Probe *p=&job[i];Entry *e=&entries[p->slot];if(e->token==p->token&&e->path&&!strcmp(e->path,p->path)){e->exists=p->exists;e->nonempty=p->nonempty;e->known=true;}}
        free(job);job=NULL;completed=launched;next=now+3;__atomic_store_n(&ready,false,__ATOMIC_RELEASE);
    }
    if(__atomic_load_n(&stop,__ATOMIC_ACQUIRE)||now<retry_after||(completed==requested&&now<next))return;
    count=0;for(int i=0;i<LIMIT;i++)if(entries[i].path&&(entries[i].permanent||entries[i].wanted))count++;
    if(!count){completed=requested;next=now+3;return;}
    job=calloc(count,sizeof *job);if(!job){retry_after=now+1;return;}
    unsigned at=0;for(int i=0;i<LIMIT;i++)if(entries[i].path&&(entries[i].permanent||entries[i].wanted)){Probe *p=&job[at++];snprintf(p->path,sizeof p->path,"%s",entries[i].path);p->slot=i;p->token=entries[i].token;}
    launched=requested;s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);worker=threadCreate(probe,NULL,32*1024,priority<0x3d?priority+2:0x3e,-2,false);
    if(!worker){free(job);job=NULL;retry_after=now+1;}
}
void file_view_stop(void){__atomic_store_n(&stop,true,__ATOMIC_RELEASE);}
#ifdef FILE_VIEW_HOST_TEST
#define thread_reap(t) (threadJoin(*(t),U64_MAX),threadFree(*(t)),*(t)=NULL,true)
#endif
// A probe still waiting for the SD at the shutdown deadline keeps its snapshot.
void file_view_exit(void){file_view_stop();if(worker&&!thread_reap(&worker))return;free(job);job=NULL;for(int i=0;i<LIMIT;i++){free(entries[i].path);memset(&entries[i],0,sizeof entries[i]);}}
