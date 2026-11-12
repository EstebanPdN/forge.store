#include "sd_usage.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
typedef struct {unsigned left;bool complete;json_t *seen;} Walk;
static bool cancelled;
void sd_usage_cancel(bool cancel){__atomic_store_n(&cancelled,cancel,__ATOMIC_RELEASE);}
static uint64_t sum(uint64_t a,uint64_t b,Walk *w){if(b>UINT64_MAX-a){w->complete=false;return UINT64_MAX;}return a+b;}
static uint64_t measure(const char *path,unsigned depth,Walk *w){
    if(!path||strlen(path)>1000||depth>8||!w->left||__atomic_load_n(&cancelled,__ATOMIC_ACQUIRE)){w->complete=false;return 0;}w->left--;
    struct stat st;if(lstat(path,&st)){if(errno!=ENOENT)w->complete=false;return 0;}
    if(S_ISLNK(st.st_mode))return 0;
    if(S_ISREG(st.st_mode)){if(json_object_get(w->seen,path))return 0;json_object_set_new(w->seen,path,json_true());return st.st_size>0?(uint64_t)st.st_size:0;}
    if(!S_ISDIR(st.st_mode))return 0;
    DIR *d=opendir(path);if(!d){w->complete=false;return 0;}uint64_t total=0;struct dirent *entry;
    while((entry=readdir(d))){if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;if(!w->left){w->complete=false;break;}char child[1100];int n=snprintf(child,sizeof child,"%s%s%s",path,strlen(path)&&path[strlen(path)-1]=='/'?"":"/",entry->d_name);if(n<0||(size_t)n>=sizeof child){w->complete=false;continue;}total=sum(total,measure(child,depth+1,w),w);}
    closedir(d);return total;
}
SdUsage sd_usage_measure(const char *data_dir,json_t *installed,json_t *user_paths){
    Walk w={.left=8192,.complete=true,.seen=json_object()};SdUsage out={0};if(!w.seen)return out;
    out.store=measure(data_dir,0,&w);const char *key;json_t *record;size_t i;json_t *v;
    json_object_foreach(installed,key,record){json_array_foreach(json_object_get(record,"files"),i,v){const char *p=json_string_value(v);char full[1100];if(p&&p[0]=='/'&&!strncmp(data_dir,"sdmc:",5)){int n=snprintf(full,sizeof full,"sdmc:%s",p);if(n<0||(size_t)n>=sizeof full){w.complete=false;continue;}p=full;}if(p&&strncmp(p,"title:",6))out.store=sum(out.store,measure(p,0,&w),&w);}}
    json_array_foreach(user_paths,i,v){const char *p=json_string_value(v);if(p)out.user=sum(out.user,measure(p,0,&w),&w);}
    out.complete=w.complete;json_decref(w.seen);return out;
}
uint64_t sd_usage_path(const char *path,unsigned limit,bool *complete){Walk w={.left=limit,.complete=true,.seen=json_object()};uint64_t n=w.seen?measure(path,0,&w):0;if(complete)*complete=w.seen&&w.complete;json_decref(w.seen);return n;}
