#include "../source/universal_convert.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *s(json_t *j,const char *k){return json_string_value(json_object_get(j,k));}
static void edges(void){
    json_t *full=json_loads("[{\"slug\":\"fixture\",\"title\":\"Fixture\",\"systems\":\"['3DS']\",\"downloads\":\"{'Fixture.cia': {'size': 123}}\",\"updated\":\"2026-11-10\",\"update_notes_md\":\"* 消除宏\\n* Keep **this** [link](https://example.com)\"}]",0,NULL);
    json_t *store=json_loads("{\"storeContent\":[{\"info\":{\"title\":\"Fixture\"},\"Fixture.cia\":[{\"type\":\"downloadRelease\",\"repo\":\"a/b\",\"file\":\"Fixture.*\\\\.cia\",\"output\":\"sdmc:/tmp/Fixture.cia\"},{\"type\":\"installCia\",\"file\":\"sdmc:/tmp/Fixture.cia\"}]}]}",0,NULL);
    assert(full&&store);json_t *apps=universal_convert(full,store);assert(apps&&json_array_size(apps)==1);json_t *app=json_array_get(apps,0),*release=json_object_get(json_object_get(app,"releases"),"stable"),*steps=json_object_get(json_object_get(app,"install"),"cia");
    assert(json_integer_value(json_object_get(release,"size"))==123);assert(!strcmp(s(json_array_get(steps,0),"repo"),"a/b"));assert(!strcmp(s(json_array_get(steps,0),"output"),"/tmp/Fixture.cia"));assert(!strcmp(json_string_value(json_array_get(json_object_get(json_object_get(release,"notes"),"en"),0)),"Keep this link"));json_decref(apps);
    json_array_append(full,json_array_get(full,0));apps=universal_convert(full,store);assert(json_array_size(apps)==1);json_decref(apps);
    json_t *raw=json_object_get(json_array_get(json_object_get(store,"storeContent"),0),"Fixture.cia");json_array_append_new(raw,json_pack("{s:s}","type","promptMessage"));apps=universal_convert(full,store);app=json_array_get(apps,0);assert(json_is_true(json_object_get(app,"nativeUnsupported")));json_decref(apps);
    json_object_set_new(json_array_get(full,0),"systems",json_pack("[s]","DS"));apps=universal_convert(full,store);assert(!json_array_size(apps));json_decref(apps);
    assert(!universal_convert(store,full));json_decref(store);json_decref(full);
}
int main(int argc,char **argv){
    edges();assert(argc==4);json_t *full=json_load_file(argv[1],0,NULL),*store=json_load_file(argv[2],0,NULL);assert(full&&store);json_t *apps=universal_convert(full,store);assert(apps);assert(!json_dump_file(apps,argv[3],JSON_INDENT(2)));printf("PASS: %zu real apps converted; bounds, duplicates, formats, sizes, Unicode notes and unsupported scripts\n",json_array_size(apps));json_decref(apps);
    const char *keys[]={"systems","downloads","slug","title","version","stars","icon","image","update_notes_md"};
    for(int i=0;i<500;i++){json_t *copy=json_deep_copy(full),*item=json_array_get(copy,(unsigned)i%json_array_size(copy));const char *key=keys[i%9];if(i%3==0)json_object_del(item,key);else json_object_set_new(item,key,i%3==1?json_null():json_array());apps=universal_convert(copy,store);assert(apps&&json_array_size(apps)<=512);json_decref(apps);json_decref(copy);}
    json_decref(full);json_decref(store);puts("PASS: 500 production-document mutations under sanitizers");
}
