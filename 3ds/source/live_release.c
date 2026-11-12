#include "live_release.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
static const char *s(json_t *o,const char *k){const char *v=json_string_value(json_object_get(o,k));return v?v:"";}
static bool valid_tag(const char *tag){size_t n=strlen(tag);if(!n||n>120)return false;for(;*tag;tag++)if(!isalnum((unsigned char)*tag)&&!strchr("._-+",*tag))return false;return true;}
static bool replace(char *out,size_t cap,const char *source,const char *old,const char *next){
    size_t used=0,old_n=strlen(old),next_n=strlen(next);const char *cursor=source,*at;
    while(old_n&&(at=strstr(cursor,old))){size_t prefix=at-cursor;if(prefix+next_n>=cap-used)return false;memcpy(out+used,cursor,prefix);used+=prefix;memcpy(out+used,next,next_n);used+=next_n;cursor=at+old_n;}
    size_t tail=strlen(cursor);if(tail>=cap-used)return false;memcpy(out+used,cursor,tail+1);return true;
}
static json_t *retag_asset(json_t *asset,json_t *live,const char *old,const char *tag,bool nested){
        char guess[512];const char *name=s(asset,"name");
        bool has_tag=*old&&strstr(name,old);
        if(!replace(guess,sizeof guess,name,has_tag?old:*old=='v'?old+1:old,has_tag?tag:*tag=='v'?tag+1:tag))return NULL;
        json_t *match=NULL,*candidate;size_t i;
        json_array_foreach(json_object_get(live,"assets"),i,candidate)if(!strcmp(s(candidate,"name"),guess)){match=candidate;break;}
        if(!match)json_array_foreach(json_object_get(live,"assets"),i,candidate)if(!strcmp(s(candidate,"name"),s(asset,"name"))){match=candidate;break;}
        if(!match||!json_is_integer(json_object_get(match,"size"))||json_integer_value(json_object_get(match,"size"))<0)return NULL;
        json_t *updated=json_deep_copy(asset);if(!updated)return NULL;
        json_object_set(updated,"name",json_object_get(match,"name"));json_object_set(updated,"size",json_object_get(match,"size"));
        // Never retain a different version's integrity hash. GitHub's digest or
        // the release sums file must supply the new expected value.
        json_object_del(updated,"sha256");const char *digest=s(match,"digest");
        if(!strncmp(digest,"sha256:",7)&&strlen(digest+7)==64){bool hex=true;for(const char *p=digest+7;*p;p++)if(!isxdigit((unsigned char)*p))hex=false;if(hex)json_object_set_new(updated,"sha256",json_string(digest+7));}
        const char *url=s(updated,"url");if(*url){char revised[2048];if(!replace(revised,sizeof revised,url,old,tag)){json_decref(updated);return NULL;}json_object_set_new(updated,"url",json_string(revised));}
        json_t *variant=json_object_get(asset,"o3ds");
        if(variant){json_t *mapped=nested?NULL:retag_asset(variant,live,old,tag,true);if(!mapped){json_decref(updated);return NULL;}json_object_set_new(updated,"o3ds",mapped);}
        return updated;
}
static bool merge_channel(json_t *releases,const char *channel,json_t *live){
    if(!live)return false;
    const char *tag=s(live,"tag_name");if(!valid_tag(tag))return false;
    json_t *known=json_object_get(releases,channel);if(!known)known=json_object_get(releases,"stable");if(!known)known=json_object_get(releases,"experimental");
    if(!json_is_object(known)||!strcmp(s(known,"tag"),tag))return false;
    json_t *assets=json_object();if(!assets)return false;
    const char *key;json_t *asset;json_object_foreach(json_object_get(known,"assets"),key,asset){
        json_t *updated=retag_asset(asset,live,s(known,"tag"),tag,false);
        if(!updated){json_decref(assets);return false;}
        if(json_object_set_new(assets,key,updated)){json_decref(assets);return false;}
    }
    if(!json_object_size(assets)){json_decref(assets);return false;}
    json_t *updated=json_deep_copy(known);if(!updated){json_decref(assets);return false;}
    char date[11];snprintf(date,sizeof date,"%.10s",s(live,"published_at"));
    json_object_set_new(updated,"tag",json_string(tag));json_object_set_new(updated,"date",json_string(date));json_object_set_new(updated,"assets",assets);
    json_object_set_new(updated,"notes",json_pack("{s:[s],s:[s]}","es","Versión publicada en GitHub (notas en el repositorio).","en","Published on GitHub (notes in the repository)."));
    bool ok=json_object_set_new(releases,channel,updated)==0;return ok;
}
bool live_release_merge(json_t *app,json_t *github){
    // Curated archives include versioned internal paths and verified payload
    // hashes. Do not retag their metadata while retaining an older install plan.
    if(json_is_true(json_object_get(app,"releasePinned")))return false;
    json_t *releases=json_object_get(app,"releases");if(!json_is_object(releases)||!json_is_array(github))return false;
    json_t *stable=NULL,*pre=NULL,*r;size_t i;
    json_array_foreach(github,i,r){if(json_is_true(json_object_get(r,"draft"))||!strncmp(s(r,"tag_name"),"store-media-",12)||!valid_tag(s(r,"tag_name")))continue;
        if(json_is_true(json_object_get(r,"prerelease"))){if(!pre)pre=r;}else if(!stable)stable=r;
    }
    bool changed=merge_channel(releases,"stable",stable);
    const char *stable_date=s(json_object_get(releases,"stable"),"date");
    if(pre&&strncmp(s(pre,"published_at"),stable_date,10)>0)changed=merge_channel(releases,"experimental",pre)||changed;
    return changed;
}
