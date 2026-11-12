#include "catalog_sync.h"
#include "catalog.h"
#include "install.h"
#include "network.h"
#include "live_release.h"
// One independent background request, at most one per curated repo per six
// hours. The reader owns cancellation and shutdown; no UI draw does IO.
static int next_app,waiting_app=-1;
static float next_at;
static bool stopped;
bool catalog_sync_pump(void){
    const int slot=4;bool changed=false;
    if(waiting_app>=0){char error[160];char *body=reader_take_slot(slot,error,sizeof error);
        if(!body)return false;
        if(!error[0]){json_t *releases=json_loads(body,0,NULL);if(releases){changed=live_release_merge(APPS[waiting_app].j,releases);json_decref(releases);}}
        else if(strstr(error,"rate limit"))stopped=true;
        free(body);waiting_app=-1;next_at=NOW+1;
    }
    if(!stopped&&NOW>=next_at&&!install_active()&&!reader_slot_busy(slot)){
        while(next_app<BUILTIN_APPS){App *app=&APPS[next_app++];if(!app->repo||!json_is_object(json_object_get(app->j,"releases")))continue;
            char path[256];int n=snprintf(path,sizeof path,"repos/%s/releases?per_page=10",app->repo);
            if(n>0&&(size_t)n<sizeof path&&reader_request_slot_ttl(slot,path,false,21600)){waiting_app=next_app-1;break;}
        }
    }return changed;
}
