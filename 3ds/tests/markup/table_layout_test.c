#include "../../source/gh_table_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static float measure(const char*s,size_t n,unsigned char style,void*ctx){(void)s;(void)style;(void)ctx;return n*4;}
int main(void){
 GhDocument d={0};GhFlow flow={0};GhTables tables={0};
 const char *source="<table><tr><td><strong>Title</strong><br>Some words that wrap at the cell edge.</td><td>Second</td></tr><tr><td>Three</td><td>[Link](README.md)</td></tr></table>";
 assert(gh_document_parse(&d,source,"owner/repo","main",false));float height=0;
 int end=gh_table_layout(&tables,&flow,&d,0,12,200,40,&height,measure,NULL,NULL);
 assert(end==d.count&&tables.count==4&&!tables.truncated);assert(tables.cells[0].w==100&&tables.cells[1].x==112);
 assert(tables.cells[0].y==40&&tables.cells[0].h==tables.cells[1].h&&tables.cells[2].y==40+tables.cells[0].h);
 assert(height==tables.cells[0].h+tables.cells[2].h);
 for(unsigned i=0;i<flow.count;i++){GhFlowRun*r=&flow.runs[i];assert(r->node<d.count&&r->end<=strlen(d.nodes[r->node].text));unsigned col=d.nodes[r->node].table_col;assert(r->x>=17.5f+col*100&&r->x+measure(NULL,r->end-r->begin,0,NULL)<=106.5f+col*100);}
 int links=0;for(unsigned i=0;i<tables.placements;i++){GhTableNode*p=&tables.nodes[i];assert(p->node<(unsigned)d.count);if(d.nodes[p->node].kind==GH_LINK){links++;assert(p->count&&p->h>0);}}
 assert(links==1);gh_document_free(&d);gh_flow_reset(&flow);memset(&tables,0,sizeof tables);
 char large[22000]="|A|B|C|D|E|F|G|H|\\n"; // Build directly below with actual newlines.
 strcpy(large,"|A|B|C|D|E|F|G|H|\n|-|-|-|-|-|-|-|-|\n");
 for(int i=0;i<100;i++)strcat(large,"|x|x|x|x|x|x|x|x|\n");
 assert(gh_document_parse(&d,large,"owner/repo","main",false));end=gh_table_layout(&tables,&flow,&d,0,12,376,0,&height,measure,NULL,NULL);
 assert(end==d.count&&tables.count==256&&tables.truncated&&height>0);
 gh_document_free(&d);gh_flow_free(&flow);puts("PASS: HTML/Markdown table geometry, shared row heights, inline links, wrapped cells and bounded truncation");
}
