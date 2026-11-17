#include "catalog_sync.h"
#include "catalog.h"
#include "install.h"
#include "network.h"
#include "live_release.h"
// One independent background request at a time, cycling through the curated repos every
// 30 minutes; GitHub answers unchanged lists with 304, which does not use the rate limit. The reader owns cancellation and shutdown; no UI draw does IO.
static int next_app,waiting_app=-1,priority_app=-1;
static float next_at,limited_until,cycle_at;
// A failed download asks again for that one repository, bypassing the 6-hour cache:
// a deleted pre-release must not keep sending the installer to a 404.
void catalog_sync_refresh(App *app){if(app&&app>=APPS&&app<APPS+BUILTIN_APPS)priority_app=(int)(app-APPS);}
bool catalog_sync_pump(void){
    const int slot=4;bool changed=false;
    if(waiting_app>=0){char error[160];char *body=reader_take_slot(slot,error,sizeof error);
        if(!body)return false;
        if(!error[0]){json_t *releases=json_loads(body,0,NULL);if(releases){changed=live_release_merge(APPS[waiting_app].j,releases);if(changed)app_refresh_updated(&APPS[waiting_app]);json_decref(releases);}}
        else if(strstr(error,"rate limit")||strstr(error,"retry later"))limited_until=NOW+600;
        free(body);waiting_app=-1;next_at=NOW+1;
    }
    if(priority_app>=0&&waiting_app<0&&!reader_slot_busy(slot)){App *app=&APPS[priority_app];char path[256];
        int n=app->repo?snprintf(path,sizeof path,"repos/%s/releases?per_page=12",app->repo):-1;
        if(n>0&&(size_t)n<sizeof path&&reader_request_slot_ttl(slot,path,false,1))waiting_app=priority_app;
        priority_app=-1;return changed;
    }
    if(next_app>=BUILTIN_APPS&&NOW>=cycle_at){next_app=0;cycle_at=NOW+1800;}
    if(NOW>=limited_until&&NOW>=next_at&&!install_active()&&!reader_slot_busy(slot)){
        while(next_app<BUILTIN_APPS){App *app=&APPS[next_app++];if(!app->repo||!json_object_size(json_object_get(app->j,"releases"))||json_is_true(json_object_get(app->j,"releasePinned")))continue;
            char path[256];int n=snprintf(path,sizeof path,"repos/%s/releases?per_page=12",app->repo);
            if(n>0&&(size_t)n<sizeof path&&reader_request_slot_ttl(slot,path,false,3600)){waiting_app=next_app-1;break;}
        }
    }return changed;
}
