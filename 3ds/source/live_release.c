#include "live_release.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
static const char *s(json_t *o,const char *k){const char *v=json_string_value(json_object_get(o,k));return v?v:"";}
static bool valid_tag(const char *tag){size_t n=strlen(tag);if(!n||n>120)return false;for(;*tag;tag++)if(!isalnum((unsigned char)*tag)&&!strchr("._-+",*tag))return false;return true;}
static bool replace(char *out,size_t cap,const char *source,const char *old,const char *next){
    size_t used=0,old_n=strlen(old),next_n=strlen(next);const char *cursor=source,*at;
    while(old_n&&(at=strstr(cursor,old))){size_t prefix=at-cursor;if(prefix+next_n>=cap-used)return false;memcpy(out+used,cursor,prefix);used+=prefix;memcpy(out+used,next,next_n);used+=next_n;cursor=at+old_n;}
    size_t tail=strlen(cursor);if(tail>=cap-used)return false;memcpy(out+used,cursor,tail+1);return true;
}
static json_t *retag_asset(json_t *asset,json_t *live,const char *old,const char *tag,bool single_cia){
        char guess[512];const char *name=s(asset,"name");
        bool has_tag=*old&&strstr(name,old);
        if(!replace(guess,sizeof guess,name,has_tag?old:*old=='v'?old+1:old,has_tag?tag:*tag=='v'?tag+1:tag))return NULL;
        json_t *match=NULL,*candidate;size_t i;
        json_array_foreach(json_object_get(live,"assets"),i,candidate)if(!strcmp(s(candidate,"name"),guess)){match=candidate;break;}
        if(!match)json_array_foreach(json_object_get(live,"assets"),i,candidate)if(!strcmp(s(candidate,"name"),s(asset,"name"))){match=candidate;break;}
        if(!match){
            json_array_foreach(json_object_get(live,"assets"),i,candidate)
                if(!strcasecmp(s(candidate,"name"),guess)){if(match)return NULL;match=candidate;}
        }
        if(!match && single_cia && strlen(name)>4 && !strcasecmp(name+strlen(name)-4,".cia")){
            json_array_foreach(json_object_get(live,"assets"),i,candidate){
                const char *n=s(candidate,"name");size_t len=strlen(n);
                if(len>4 && !strcasecmp(n+len-4,".cia")){if(match)return NULL;match=candidate;}
            }
        }
        if(!match||!json_is_integer(json_object_get(match,"size"))||json_integer_value(json_object_get(match,"size"))<0)return NULL;
        json_t *updated=json_deep_copy(asset);if(!updated)return NULL;
        json_object_set(updated,"name",json_object_get(match,"name"));json_object_set(updated,"size",json_object_get(match,"size"));
        // Never retain a different version's integrity hash. GitHub's digest or
        // the release sums file must supply the new expected value.
        if(strcmp(old,tag) || strcmp(name,s(match,"name")) || !json_equal(json_object_get(asset,"size"),json_object_get(match,"size")))json_object_del(updated,"sha256");
        const char *digest=s(match,"digest");
        if(!strncmp(digest,"sha256:",7)&&strlen(digest+7)==64){bool hex=true;for(const char *p=digest+7;*p;p++)if(!isxdigit((unsigned char)*p))hex=false;if(hex)json_object_set_new(updated,"sha256",json_string(digest+7));}
        if(strcmp(old,tag) || strcmp(name,s(match,"name")) || strcmp(s(updated,"sha256"),s(asset,"sha256")))json_object_del(updated,"sumsName");
        const char *url=s(updated,"url");if(*url){char revised[2048];if(!replace(revised,sizeof revised,url,old,tag)){json_decref(updated);return NULL;}json_object_set_new(updated,"url",json_string(*s(match,"browser_download_url")?s(match,"browser_download_url"):revised));}
        json_t *variant=json_object_get(asset,"o3ds");
        if(variant){json_t *mapped=json_object_get(variant,"o3ds")?NULL:retag_asset(variant,live,old,tag,false);if(!mapped){json_decref(updated);return NULL;}json_object_set_new(updated,"o3ds",mapped);}
        return updated;
}
static bool merge_channel(json_t *releases,const char *channel,json_t *live){
    if(!live)return false;
    const char *tag=s(live,"tag_name");if(!valid_tag(tag))return false;
    json_t *known=json_object_get(releases,channel);if(!known)known=json_object_get(releases,"stable");if(!known)known=json_object_get(releases,"experimental");
    if(!json_is_object(known))return false;
    if(known==json_object_get(releases,channel) && strcmp(s(known,"tag"),tag) &&
       strcmp(s(live,"published_at"),s(known,"publishedAt"))<0)return false;
    json_t *assets=json_object();if(!assets)return false;
    const char *key;json_t *asset;json_object_foreach(json_object_get(known,"assets"),key,asset){
        json_t *updated=retag_asset(asset,live,s(known,"tag"),tag,!strcmp(key,"cia")&&!json_object_get(asset,"o3ds"));
        if(!updated){json_decref(assets);return false;}
        if(json_object_set_new(assets,key,updated)){json_decref(assets);return false;}
    }
    bool same=!strcmp(s(known,"tag"),tag) && known==json_object_get(releases,channel);
    const char *revision="";
    size_t i;json_t *candidate;
    json_array_foreach(json_object_get(live,"assets"),i,candidate)
        if(strcmp(s(candidate,"updated_at"),revision)>0)revision=s(candidate,"updated_at");
    if(same && strcmp(revision,s(known,"assetUpdated"))<0){json_decref(assets);return false;}
    if((same && !strcmp(s(live,"published_at"),s(known,"publishedAt")) && !strcmp(revision,s(known,"assetUpdated")) && json_equal(assets,json_object_get(known,"assets"))) || !json_object_size(assets)){json_decref(assets);return false;}
    json_t *updated=json_deep_copy(known);if(!updated){json_decref(assets);return false;}
    char date[11];snprintf(date,sizeof date,"%.10s",s(live,"published_at"));
    json_object_set_new(updated,"tag",json_string(tag));json_object_set_new(updated,"date",json_string(date));json_object_set_new(updated,"assets",assets);
    json_object_set_new(updated,"publishedAt",json_string(s(live,"published_at")));
    if(*revision)json_object_set_new(updated,"assetUpdated",json_string(revision));
    if(!same || !json_equal(json_object_get(json_object_get(known,"assets"),"cia"),json_object_get(assets,"cia"))){json_object_del(updated,"ciaTitle");json_object_del(updated,"ciaVersion");}
    if(!same)json_object_set_new(updated,"notes",json_pack("{s:[s],s:[s],s:[s],s:[s],s:[s],s:[s],s:[s],s:[s]}","es","Consulta el historial completo en GitHub.","en","Read the full changelog on GitHub.","pt","Veja o histórico completo no GitHub.","it","Leggi il changelog completo su GitHub.","fr","Consultez l’historique complet sur GitHub.","de","Den vollständigen Changelog gibt es auf GitHub.","ru","Полный список изменений — на GitHub.","ja","変更履歴の全文は GitHub をご覧ください。"));
    bool ok=json_object_set_new(releases,channel,updated)==0;return ok;
}
bool live_release_merge(json_t *app,json_t *github){
    // Curated archives include versioned internal paths and verified payload
    // hashes. Do not retag their metadata while retaining an older install plan.
    if(json_is_true(json_object_get(app,"releasePinned")))return false;
    json_t *releases=json_object_get(app,"releases");if(!json_is_object(releases)||!json_is_array(github))return false;
    json_t *stable=NULL,*pre=NULL,*r;size_t i;
    json_array_foreach(github,i,r){if(json_is_true(json_object_get(r,"draft"))||!strncmp(s(r,"tag_name"),"store-media-",12)||!valid_tag(s(r,"tag_name")))continue;
        if(json_is_true(json_object_get(r,"prerelease"))){if(!pre || strcmp(s(r,"published_at"),s(pre,"published_at"))>0)pre=r;}
        else if(!stable || strcmp(s(r,"published_at"),s(stable,"published_at"))>0)stable=r;
    }
    bool changed=merge_channel(releases,"stable",stable);
    json_t *selected=json_object_get(releases,"stable");
    const char *stable_date=*s(selected,"publishedAt")?s(selected,"publishedAt"):s(selected,"date");
    if(pre&&strcmp(s(pre,"published_at"),stable_date)>0)changed=merge_channel(releases,"experimental",pre)||changed;
    return changed;
}
