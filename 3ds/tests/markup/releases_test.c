#include "../../source/gh_releases.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    GhDocument doc={0};json_t *rels=json_array();
    for(int i=0;i<15;i++){
        json_t *rel=json_pack("{s:s,s:s,s:b}","name","v4.1","body","## Changelog\n**Bold** and [features](#features)\n![image](image.png)","draft",i==1);
        json_t *assets=json_array();for(int k=0;k<9;k++)json_array_append_new(assets,json_pack("{s:s}","name","package.cia"));
        json_object_set_new(rel,"assets",assets);json_array_append_new(rels,rel);
    }
    assert(gh_document_releases(&doc,rels,"owner/repo","main",true));
    int cards=0,ends=0,assets=0,links=0;
    for(int i=0;i<doc.count;i++){GhNode *n=&doc.nodes[i];cards+=n->kind==GH_RELEASE;ends+=n->kind==GH_RELEASE_END;assets+=n->kind==GH_ASSET;links+=n->kind==GH_LINK;assert(n->text);}
    assert(cards==11&&ends==cards&&assets==cards*6&&links==cards);
    char *large=malloc(GH_DOC_BYTES+200);memset(large,'x',GH_DOC_BYTES+199);large[GH_DOC_BYTES+199]=0;
    json_object_set_new(json_array_get(rels,0),"body",json_string(large));free(large);
    assert(gh_document_releases(&doc,rels,"owner/repo","main",false));
    assert(doc.truncated&&doc.bytes<=GH_DOC_BYTES&&doc.count<=GH_DOC_NODES);
    gh_document_free(&doc);assert(gh_document_releases(&doc,NULL,"owner/repo","main",true)&&doc.count==0);
    gh_document_free(&doc);json_decref(rels);puts("PASS: bounded release cards, six assets, bodies, drafts, oversized data, repeated cleanup");
}
