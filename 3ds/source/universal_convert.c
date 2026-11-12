#include "universal_convert.h"
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define MB (1024*1024)
static const char *s(json_t *j,const char *k){const char *p=json_string_value(json_object_get(j,k));return p?p:"";}
static void put(json_t *j,const char *k,const char *v){json_object_set_new(j,k,json_string(v?v:""));}
static const char *out(const char *p){return !strncmp(p,"sdmc:",5)?p+5:p;}
static const char *base(const char *p){const char *q=strrchr(p,'/');return q?q+1:p;}
static bool ends(const char *s,const char *suffix){size_t n=strlen(s),m=strlen(suffix);return n>=m&&!strcasecmp(s+n-m,suffix);}
static bool contains_ci(const char *s,const char *needle){for(;*s;s++)if(!strncasecmp(s,needle,strlen(needle)))return true;return false;}
static bool system3ds(json_t *j){
    if(json_is_array(j)){size_t i;json_t *v;json_array_foreach(j,i,v){const char *p=json_string_value(v);if(p&&!strcasecmp(p,"3DS"))return true;}return false;}
    const char *p=json_string_value(j);if(!p)return false;
    while((p=strchr(p,'\''))){const char *e=strchr(++p,'\'');if(!e)break;if(e-p==3&&!strncasecmp(p,"3DS",3))return true;p=e+1;}return false;
}
static json_t *downloads(json_t *value){
    if(json_is_object(value))return json_incref(value);
    json_t *o=json_object();const char *p=json_string_value(value);if(!p)return o;
    // full.json's Python representation has the exact form 'name': {'size': N}.
    while((p=strchr(p,'\''))){const char *e=strchr(++p,'\'');if(!e)break;
        if(e-p>0&&e-p<512&&!strncmp(e,"': {'size': ",12)){char key[512];memcpy(key,p,e-p);key[e-p]=0;char *end;long long n=strtoll(e+12,&end,10);if(end>e+12&&n>=0)json_object_set_new(o,key,json_pack("{s:I}","size",(json_int_t)n));p=end;}else p=e+1;
    }return o;
}
// Size hints only: a bounded, non-recursive subset for literal names, anchors,
// escaped dots and .* patterns. Unsupported expressions use the web's first-size
// fallback; install asset selection remains the installer's independent check.
static bool size_match(const char *re,const char *name){
    size_t n=strlen(name);if(n>1024||strlen(re)>512)return false;bool a[1025],b[1025];bool anchored=*re=='^';if(anchored)re++;
    for(size_t i=0;i<=n;i++)a[i]=!anchored||i==0;
    while(*re){if(*re=='$'&&!re[1])return a[n];memset(b,0,n+1);
        if(re[0]=='.'&&re[1]=='*'){b[0]=a[0];for(size_t i=1;i<=n;i++)b[i]=a[i]||b[i-1];re+=2;}
        else{char want=*re++;bool any=want=='.';if(want=='\\'&&*re){want=*re++;any=false;}else if(strchr("*+?[]{}()|",want))return false;for(size_t i=1;i<=n;i++)b[i]=a[i-1]&&(any||name[i-1]==want);}
        memcpy(a,b,n+1);
    }for(size_t i=0;i<=n;i++)if(a[i])return true;return false;
}
static json_int_t guess(const char *name,json_t *d){
    if(!*name)return 2*MB;
    json_t *hit=json_object_get(d,base(name));
    if(hit){json_t *n=json_object_get(hit,"size");return json_is_integer(n)?json_integer_value(n):-1;}
    {const char *k;json_t *v;json_object_foreach(d,k,v)if(size_match(name,k)){json_t *n=json_object_get(v,"size");return json_is_integer(n)?json_integer_value(n):-1;}}
    const char *k;json_t *v;json_object_foreach(d,k,v){json_t *n=json_object_get(v,"size");return json_is_integer(n)?json_integer_value(n):-1;}return 2*MB;
}
static bool supported(const char *t){const char *types[]={"downloadFile","downloadRelease","extractFile","installCia","deleteFile","mkdir","rmdir","move"};for(int i=0;i<8;i++)if(!strcmp(t,types[i]))return true;return false;}
static json_t *size_json(json_int_t n){return n<0?json_null():json_integer(n);}
static json_t *step(json_t *v,json_t *d,bool native_safe,bool *unsupported){
    const char *type=s(v,"type");json_t *r=json_object();put(r,"type",type);
    if(!strcmp(type,"downloadFile")||!strcmp(type,"downloadRelease")){
        bool release=!strcmp(type,"downloadRelease");const char *file=s(v,"file"),*path=out(s(v,"output"));char name[1024];snprintf(name,sizeof name,"%s",base(release?path:file));if(!release){char *q=strchr(name,'?');if(q)*q=0;}
        put(r,"type","downloadUrl");put(r,"name",name);put(r,"url",release?"":file);put(r,"output",path);json_int_t hint=guess(release?file:base(file),d);if(hint>=0)json_object_set_new(r,"size",json_integer(hint));
        if(release){if(native_safe){put(r,"repo",s(v,"repo"));put(r,"match",file);}else{char url[2048];snprintf(url,sizeof url,"https://github.com/%s/releases/latest",s(v,"repo"));put(r,"url",url);}}
    }else if(!strcmp(type,"extractFile")){
        const char *file=out(s(v,"file")),*target=out(s(v,"output"));char path[2048],archive[1024];snprintf(archive,sizeof archive,"%s",base(file));
        const char *suffixes[]={".tar.gz",".zip",".7z",".rar",".tar"};for(int i=0;i<5;i++)if(ends(archive,suffixes[i])){archive[strlen(archive)-strlen(suffixes[i])]=0;break;}
        snprintf(path,sizeof path,"%s%s",target,ends(target,"/")?archive:"");put(r,"file",file);put(r,"output",target);
        if(native_safe&&*s(v,"input"))put(r,"input",s(v,"input"));
        json_int_t size=guess(base(file),d);json_t *creates=json_pack("{s:s}","path",path);json_object_set_new(creates,"size",size_json(size<0?-1:(json_int_t)floor(size*1.4+.5)));json_t *list=json_array();json_array_append_new(list,creates);json_object_set_new(r,"creates",list);
    }else if(!strcmp(type,"installCia")||!strcmp(type,"deleteFile"))put(r,"file",out(s(v,"file")));
    else if(!strcmp(type,"mkdir")||!strcmp(type,"rmdir"))put(r,"directory",out(s(v,"directory")));
    else if(!strcmp(type,"move")){put(r,"old",out(s(v,"old")));put(r,"new",out(s(v,"new")));}
    else{*unsupported=true;json_decref(r);return NULL;}return r;
}
static size_t utf16_length(const char *s){size_t n=0;for(const unsigned char *p=(const unsigned char *)s;*p;p++)if((*p&0xc0)!=0x80)n+=*p>=0xf0?2:1;return n;}
static json_t *notes(const char *markdown){
    json_t *a=json_array();const char *p=markdown;
    while(*p&&json_array_size(a)<6){const char *e=strchr(p,'\n');if(!e)e=p+strlen(p);while(p<e&&isspace((unsigned char)*p))p++;const char *end=e;while(end>p&&isspace((unsigned char)end[-1]))end--;
        if(end-p>2&&(p[0]=='-'||p[0]=='*')&&p[1]==' '){p+=2;char b[8192];size_t n=0;
            while(p<end&&n<sizeof b-1){if(*p=='`'){p++;continue;}if(end-p>=2&&p[0]=='*'&&p[1]=='*'){p+=2;continue;}
                if(*p=='['){const char *close=memchr(p,']',end-p);if(close&&close+1<end&&close[1]=='('){const char *link=memchr(close+2,')',end-close-2);if(link){size_t z=close-p-1;if(z>sizeof b-1-n)z=sizeof b-1-n;memcpy(b+n,p+1,z);n+=z;p=link+1;continue;}}}b[n++]=*p++;
            }b[n]=0;if(utf16_length(b)>3)json_array_append_new(a,json_string(b));
        }p=*e?e+1:e;
    }if(!json_array_size(a))json_array_append_new(a,json_string("—"));return a;
}
static void darker(char out[8],const char *hex,double amount){unsigned n=0x556070;if(strlen(hex)==7&&hex[0]=='#')sscanf(hex+1,"%x",&n);unsigned r=(unsigned)floor((n>>16)*(1-amount)+.5),g=(unsigned)floor(((n>>8)&255)*(1-amount)+.5),b=(unsigned)floor((n&255)*(1-amount)+.5);snprintf(out,8,"#%02x%02x%02x",r&255,g&255,b&255);}
static json_t *convert(json_t *item,json_t *entry){
    const char *slug=s(item,"slug"),*title=s(item,"title");if(!*slug||strlen(slug)>128||!*title||strlen(title)>512)return NULL;
    for(const char *p=slug;*p;p++)if(!(isalnum((unsigned char)*p)||*p=='-'||*p=='_'||*p=='.'))return NULL;
    json_t *d=downloads(json_object_get(item,"downloads")),*install=json_object();bool unsupported=false;
    const char *key;json_t *value;json_object_foreach(entry,key,value){
        if(!strcmp(key,"info")||contains_ci(key,"nightly")||contains_ci(key,"[git]")||contains_ci(key,"[prerelease]"))continue;
        const char *format=ends(key,".cia")?"cia":ends(key,".3dsx")?"3dsx":NULL;if(!format||json_object_get(install,format))continue;
        json_t *script=json_is_array(value)?value:json_object_get(value,"script");if(!json_is_array(script))continue;if(json_array_size(script)>512){unsupported=true;continue;}
        json_t *steps=json_array();bool download=false,native_safe=true;size_t i;json_t *v;
        json_array_foreach(script,i,v)if(!supported(s(v,"type")))native_safe=false;
        unsupported|=!native_safe;
        json_array_foreach(script,i,v){json_t *st=step(v,d,native_safe,&unsupported);if(st){download|=!strcmp(s(st,"type"),"downloadUrl");json_array_append_new(steps,st);}}
        if(download)json_object_set_new(install,format,steps);else json_decref(steps);
    }json_decref(d);if(!json_object_size(install)){json_decref(install);return NULL;}
    json_t *r=json_object();char id[136];snprintf(id,sizeof id,"udb-%s",slug);put(r,"id",id);put(r,"title",title);put(r,"category","universal");put(r,"source","universal");put(r,"author",*s(item,"author")?s(item,"author"):"—");
    const char *color=*s(item,"color")?s(item,"color"):"#556070";char dark[8],darker_[8];darker(dark,color,.45);darker(darker_,color,.7);put(r,"color",color);put(r,"status","released");
    json_object_set_new(r,"repo",*s(item,"github")?json_string(s(item,"github")):json_null());
    json_t *stars=json_object_get(item,"stars");double n=json_is_number(stars)?json_number_value(stars):atof(s(item,"stars"));json_object_set_new(r,"stars",n?json_integer((json_int_t)n):json_null());
    const char *license=*s(item,"license_name")?s(item,"license_name"):s(item,"license");json_object_set_new(r,"license",*license?json_string(license):json_null());
    json_t *icon=json_object_get(item,"icon");json_object_set_new(r,"icon",icon&&!json_is_null(icon)&&(!json_is_string(icon)||*json_string_value(icon))?json_deep_copy(icon):json_pack("{s:s,s:[s,s],s:s}","glyph","box","bg",color,dark,"fg","#ffffff"));
    json_object_set_new(r,"pixelIcon",json_true());json_object_set_new(r,"banner",*s(item,"image")?json_pack("{s:s,s:s,s:b}","bg",s(item,"image"),"bgPos","50% 50%","pixel",1):json_pack("{s:[s,s,s],s:s}","gradient",darker_,color,"#ffffff","fx","pixels"));
    json_object_set_new(r,"shots",json_array());json_object_set_new(r,"tagline",json_pack("{s:s}","en",s(item,"description")));json_object_set_new(r,"description",json_pack("{s:s}","en",s(item,"description")));json_object_set_new(r,"features",json_pack("{s:[]}","en"));json_object_set_new(r,"compat",json_pack("{s:b,s:b,s:b}","o3ds",1,"n3ds",1,"stereo",0));put(r,"folder","/3ds/");
    const char *notice=s(json_object_get(entry,"info"),"preinstall_message");if(*notice)json_object_set_new(r,"notice",json_pack("{s:s}","en",notice));
    char date[11];snprintf(date,sizeof date,"%.10s",*s(item,"updated")?s(item,"updated"):"2025-01-01");json_int_t size=0;json_t *first=json_object_iter_value(json_object_iter(install));size_t i;json_t *v;bool known=true;json_array_foreach(first,i,v)if(!strcmp(s(v,"type"),"downloadUrl")){json_t *n=json_object_get(v,"size");known&=json_is_integer(n);size+=json_integer_value(n);}
    json_t *rel=json_pack("{s:s,s:s,s:{}}","tag",*s(item,"version")?s(item,"version"):"latest","date",date,"assets");json_object_set_new(rel,"size",size_json(known?size:-1));json_t *ns=json_object();json_object_set_new(ns,"en",notes(s(item,"update_notes_md")));json_object_set_new(rel,"notes",ns);
    json_t *rels=json_object();json_object_set_new(rels,"stable",rel);json_object_set_new(r,"releases",rels);json_object_set_new(r,"requires",json_array());json_object_set_new(r,"install",install);if(unsupported)json_object_set_new(r,"nativeUnsupported",json_true());return r;
}
json_t *universal_convert(json_t *full,json_t *store){
    json_t *entries=json_object_get(store,"storeContent");if(!json_is_array(full)||json_array_size(full)>2048||!json_is_array(entries)||json_array_size(entries)>2048)return NULL;
    json_t *lookup=json_object(),*seen=json_object(),*apps=json_array();size_t i;json_t *v;
    json_array_foreach(entries,i,v){const char *title=s(json_object_get(v,"info"),"title");if(*title)json_object_set(lookup,title,v);}
    json_array_foreach(full,i,v){if(!system3ds(json_object_get(v,"systems")))continue;json_t *app=convert(v,json_object_get(lookup,s(v,"title")));if(!app)continue;const char *id=s(app,"id");if(json_object_get(seen,id)){json_decref(app);continue;}json_object_set_new(seen,id,json_true());
        if(json_array_size(apps)>=512){json_decref(app);continue;}size_t at=0;json_int_t stars=json_integer_value(json_object_get(app,"stars"));while(at<json_array_size(apps)&&json_integer_value(json_object_get(json_array_get(apps,at),"stars"))>=stars)at++;json_array_insert_new(apps,at,app);
    }json_decref(lookup);json_decref(seen);return apps;
}
