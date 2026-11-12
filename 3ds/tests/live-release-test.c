#include "../source/live_release.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *str(json_t *j,const char *key){return json_string_value(json_object_get(j,key));}
static json_t *base(void){return json_loads("{\"releases\":{\"stable\":{\"tag\":\"v4.0\",\"date\":\"2026-11-01\",\"assets\":{\"cia\":{\"name\":\"game-v4.0.cia\",\"size\":10,\"url\":\"https://example.com/v4.0/game-v4.0.cia\",\"sha256\":\"OLD\"},\"sums\":{\"name\":\"SHA256SUMS.txt\",\"size\":1}}}}}",0,NULL);}
static json_t *live(const char *tag,bool pre,const char *date){
    char name[128];snprintf(name,sizeof name,"game-%s.cia",tag);
    return json_pack("{s:s,s:b,s:s,s:[{s:s,s:i,s:s},{s:s,s:i}]}","tag_name",tag,"prerelease",pre,"published_at",date,"assets","name",name,"size",20,"digest","sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","name","SHA256SUMS.txt","size",2);
}
int main(void){
    json_t *app=base(),*rels=json_array();assert(app);json_array_append_new(rels,live("v4.0.1",false,"2026-11-08T00:00:00Z"));
    assert(live_release_merge(app,rels));json_t *stable=json_object_get(json_object_get(app,"releases"),"stable"),*cia=json_object_get(json_object_get(stable,"assets"),"cia");
    assert(!strcmp(str(stable,"tag"),"v4.0.1")&&!strcmp(str(cia,"name"),"game-v4.0.1.cia"));assert(!strcmp(str(cia,"url"),"https://example.com/v4.0.1/game-v4.0.1.cia"));assert(strlen(str(cia,"sha256"))==64);assert(!live_release_merge(app,rels));json_decref(app);json_decref(rels);
    app=base();rels=json_array();json_t *r=live("v5.0",false,"2026-11-09T00:00:00Z");json_array_remove(json_object_get(r,"assets"),1);json_array_append_new(rels,r);assert(!live_release_merge(app,rels));assert(!strcmp(str(json_object_get(json_object_get(app,"releases"),"stable"),"tag"),"v4.0"));json_decref(rels);
    rels=json_array();json_array_append_new(rels,live("store-media-v1",false,"2026-11-10"));r=live("v9.0",false,"2026-11-10");json_object_set_new(r,"draft",json_true());json_array_append_new(rels,r);assert(!live_release_merge(app,rels));json_decref(rels);
    rels=json_array();json_array_append_new(rels,live("v4.1-E1",true,"2026-11-09T00:00:00Z"));assert(live_release_merge(app,rels));assert(json_object_get(json_object_get(app,"releases"),"experimental"));json_decref(rels);json_decref(app);
    app=base();rels=json_array();json_array_append_new(rels,live("v3.0-pre",true,"2026-10-01"));assert(!live_release_merge(app,rels));json_decref(rels);
    rels=json_array();r=live("v5.0",false,"2026-11-09");json_object_del(json_array_get(json_object_get(r,"assets"),0),"digest");json_array_append_new(rels,r);assert(live_release_merge(app,rels));stable=json_object_get(json_object_get(app,"releases"),"stable");cia=json_object_get(json_object_get(stable,"assets"),"cia");assert(!json_object_get(cia,"sha256"));json_decref(rels);json_decref(app);
    app=base();stable=json_object_get(json_object_get(app,"releases"),"stable");cia=json_object_get(json_object_get(stable,"assets"),"cia");json_object_set_new(cia,"o3ds",json_pack("{s:s,s:i}","name","old-v4.0.cia","size",3));rels=json_array();r=live("v5.0",false,"2026-11-09");json_array_append_new(rels,r);assert(!live_release_merge(app,rels));json_array_append_new(json_object_get(r,"assets"),json_pack("{s:s,s:i}","name","old-v5.0.cia","size",4));assert(live_release_merge(app,rels));json_decref(rels);json_decref(app);
    app=base();rels=json_array();json_array_append_new(rels,live("../escape",false,"2026-11-09"));assert(!live_release_merge(app,rels));json_decref(rels);json_decref(app);
    puts("PASS: live releases, exact retagging, immutable fallback, complete assets, variants, digests, media/drafts, channel dates");
}
