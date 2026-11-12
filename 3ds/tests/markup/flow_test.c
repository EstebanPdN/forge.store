#include "../../source/gh_flow.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
static float measure(const char*s,size_t n,unsigned char style,void *ctx){(void)ctx;float w=0;for(size_t i=0;i<n;i++)if(((unsigned char)s[i]&0xc0)!=0x80)w+=(style&8?4:5);return w;}
static void check(GhFlow *f,GhDocument *d,float width){
    char expected[210000],got[210000];size_t a=0,b=0;
    for(int i=0;i<d->count;i++)for(const char*p=d->nodes[i].text;*p;p++)if(!isspace((unsigned char)*p))expected[a++]=*p;
    for(unsigned i=0;i<f->count;i++){GhFlowRun*r=&f->runs[i];assert(r->node<d->count);const char*s=d->nodes[r->node].text;assert(r->begin<r->end&&r->end<=strlen(s));assert(r->x>=12&&r->x+measure(s+r->begin,r->end-r->begin,d->nodes[r->node].style,NULL)<=12+width+.01f);
        if(i){assert(r->y>=f->runs[i-1].y);if(r->y==f->runs[i-1].y)assert(r->x>=f->runs[i-1].x);}
        for(size_t j=r->begin;j<r->end;j++)if(!isspace((unsigned char)s[j]))got[b++]=s[j];
    }
    assert(a==b&&!memcmp(expected,got,a));
}
int main(void){
    GhFlow f={0};GhNode nodes[4]={{.text="Join the ",.kind=GH_LINK},{.text="project",.style=2},{.text=" Discord for updates, testing, suggestions and support. Use ",.kind=GH_TEXT},{.text="GitHub issues",.kind=GH_LINK}};
    GhDocument d={.nodes=nodes,.count=4};float h=gh_flow_layout(&f,&d,0,4,12,100,20,15,false,measure,NULL);assert(h==75);check(&f,&d,100);
    gh_flow_reset(&f);h=gh_flow_layout(&f,&d,0,4,12,100,20,15,true,measure,NULL);assert(h==75);check(&f,&d,100);assert(f.runs[f.count-1].x>12);
    char *long_word=malloc(70001);memset(long_word,'x',70000);long_word[70000]=0;nodes[0].text=long_word;d.count=1;
    gh_flow_reset(&f);h=gh_flow_layout(&f,&d,0,1,12,100,0,15,false,measure,NULL);assert(h==52500&&f.count==3500);assert(f.runs[f.count-1].begin>65535);check(&f,&d,100);free(long_word);
    nodes[0].text="あいうえお　éñ東京語";gh_flow_reset(&f);gh_flow_layout(&f,&d,0,1,12,20,0,15,false,measure,NULL);check(&f,&d,20);
    for(unsigned i=0;i<f.count;i++)assert(((unsigned char)nodes[0].text[f.runs[i].begin]&0xc0)!=0x80);
    char text[3001];unsigned seed=1843;
    for(int n=0;n<1000;n++){for(int i=0;i<3000;i++){seed=seed*1664525+1013904223;text[i]="abc def \t\n"[seed%10];}text[3000]=0;nodes[0].text=text;gh_flow_reset(&f);float width=20+n%250;gh_flow_layout(&f,&d,0,1,12,width,0,15,n%2,measure,NULL);check(&f,&d,width);}
    assert(!gh_flow_layout(&f,&d,0,1,12,0,0,15,false,measure,NULL));
    long_word=malloc(200001);memset(long_word,'x',200000);long_word[200000]=0;nodes[0].text=long_word;gh_flow_reset(&f);gh_flow_layout(&f,&d,0,1,12,5,0,15,false,measure,NULL);assert(f.truncated&&f.count==8192);free(long_word);
    gh_flow_free(&f);gh_flow_free(&f);
    puts("PASS: mixed inline links/styles, centered lines, whitespace, UTF-8 boundaries, >64K offsets, 1000 layout mutations, bounded truncation and cleanup");
}
