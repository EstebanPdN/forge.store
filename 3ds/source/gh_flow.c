#include "gh_flow.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define FLOW_LIMIT 8192u
#define CHUNK 1020u
static size_t scalar(const char *s,size_t n){
    unsigned char a=(unsigned char)*s;size_t k=a<0x80?1:a>=0xc2&&a<=0xdf?2:a>=0xe0&&a<=0xef?3:a>=0xf0&&a<=0xf4?4:1;
    if(k>n)return 1;
    for(size_t i=1;i<k;i++)if(((unsigned char)s[i]&0xc0)!=0x80)return 1;
    return k;
}
static bool append(GhFlow *f,int node,size_t begin,size_t end,float x,float y){
    if(f->count==FLOW_LIMIT){f->truncated=true;return false;}
    if(f->count==f->capacity){unsigned cap=f->capacity?f->capacity*2:64;GhFlowRun *runs=realloc(f->runs,cap*sizeof *runs);if(!runs){f->truncated=true;return false;}f->runs=runs;f->capacity=cap;}
    f->runs[f->count++]=(GhFlowRun){node,(uint32_t)begin,(uint32_t)end,x,y};return true;
}
static void align_line(GhFlow *f,unsigned first,float used,float width,bool center){
    if(center){float dx=(width-used)/2;for(unsigned i=first;i<f->count;i++)f->runs[i].x+=dx;}
}
void gh_flow_reset(GhFlow *f){f->count=0;f->truncated=false;}
void gh_flow_free(GhFlow *f){free(f->runs);memset(f,0,sizeof *f);}
float gh_flow_layout(GhFlow *f,const GhDocument *doc,int begin,int end,float left,float width,float y,float line_height,bool center,GhFlowMeasure measure,void *context){
    if(!doc||!measure||width<=0||line_height<=0||begin<0||end>doc->count||begin>=end)return 0;
    float start=y,x=0,space=0;unsigned line=f->count;bool any=false;
    for(int node=begin;node<end;node++){
        const GhNode *n=&doc->nodes[node];const char *s=n->text?n->text:"";size_t length=strlen(s),at=0;
        while(at<length){
            if(isspace((unsigned char)s[at])){space=measure(" ",1,n->style,context);at++;continue;}
            size_t last=at;while(last<length&&!isspace((unsigned char)s[last]))last++;
            while(at<last){
                size_t cuts[CHUNK+1],nc=0,bytes=0;
                while(at+bytes<last){size_t k=scalar(s+at+bytes,last-at-bytes);if(bytes+k>CHUNK)break;bytes+=k;cuts[++nc]=bytes;}
                cuts[0]=0;
                float gap=x?space:0,w=measure(s+at,bytes,n->style,context);
                if(x&&x+gap+w>width){align_line(f,line,x,width,center);line=f->count;y+=line_height;x=0;gap=0;}
                if(w>width){
                    size_t lo=0,hi=nc;
                    while(lo<hi){size_t mid=lo+(hi-lo+1)/2;float fit=measure(s+at,cuts[mid],n->style,context);if(fit<=width)lo=mid;else hi=mid-1;}
                    if(!lo)lo=1;
                    bytes=cuts[lo];w=measure(s+at,bytes,n->style,context);
                }
                if(!append(f,node,at,at+bytes,left+x+gap,y))return y-start+line_height;
                x+=gap+w;at+=bytes;space=0;any=true;
                if(at<last){align_line(f,line,x,width,center);line=f->count;y+=line_height;x=0;}
            }
        }
    }
    align_line(f,line,x,width,center);
    return any?y-start+line_height:0;
}
