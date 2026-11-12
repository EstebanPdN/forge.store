#include "gh_table_layout.h"
#include <string.h>
static GhTableNode *place(GhTables*t,unsigned node,float x,float y,float h,unsigned first,unsigned count){
    if(t->placements==GH_DOC_NODES){t->truncated=true;return NULL;}
    GhTableNode *p=&t->nodes[t->placements++];*p=(GhTableNode){node,first,count,x,y,h};return p;
}
int gh_table_layout(GhTables*t,GhFlow*f,GhDocument*d,int start,float x,float width,float y,float*height,GhFlowMeasure measure,GhTableImageSize image_size,void*context){
    *height=0;if(start<0||start>=d->count||!d->nodes[start].table||width<=0)return start+1;
    unsigned id=d->nodes[start].table,cols=d->nodes[start].table_cols;int end=start;
    unsigned rows=0;while(end<d->count&&d->nodes[end].table==id){if((unsigned)d->nodes[end].table_row+1>rows)rows=d->nodes[end].table_row+1;end++;}
    if(!cols)cols=1;
    if(cols>8){cols=8;t->truncated=true;}if(rows>32){rows=32;t->truncated=true;}
    float top=y,cw=width/cols;
    for(unsigned row=0;row<rows;row++){
        if(t->count+cols>GH_TABLE_CELLS){t->truncated=true;*height=y-top;return end;}
        unsigned first_cell=t->count;float row_height=5;
        for(unsigned col=0;col<cols;col++){
            if(t->count==GH_TABLE_CELLS){t->truncated=true;*height=y-top;return end;}
            t->cells[t->count++]=(GhTableCell){x+col*cw,y,cw,0};
            int a=start;while(a<end&&(d->nodes[a].table_row!=row||d->nodes[a].table_col!=col))a++;
            int stop=a;while(stop<end&&d->nodes[stop].table_row==row&&d->nodes[stop].table_col==col)stop++;
            float left=x+col*cw+5.5f,cy=y+2.5f;GhTableNode *last_image=NULL;
            while(a<stop){
                GhNode*n=&d->nodes[a];
                if(n->kind==GH_IMAGE){float iw=cw-11,ih=90;if(image_size)image_size(n,cw-11,&iw,&ih,context);last_image=place(t,a,left+(cw-11-iw)/2,cy,ih,0,0);cy+=ih;a++;continue;}
                if(n->kind==GH_LINK&&!n->text[0]&&last_image){place(t,a,last_image->x,last_image->y,last_image->h,0,0);a++;continue;}
                int next=a;while(next<stop&&d->nodes[next].group==n->group&&d->nodes[next].kind!=GH_IMAGE){d->nodes[next].style|=16;if(d->nodes[next].table_header)d->nodes[next].style|=2;next++;}
                if(next==a){a++;continue;}
                unsigned first=f->count;float h=gh_flow_layout(f,d,a,next,left,cw-11,cy,12,false,measure,context);
                for(int i=a;i<next;i++){
                    unsigned begin=first,count=0;float px=left,py=cy,ph=0;
                    while(begin<f->count&&f->runs[begin].node!=(unsigned)i)begin++;
                    for(unsigned k=begin;k<f->count&&f->runs[k].node==(unsigned)i;k++){if(!count){px=f->runs[k].x;py=f->runs[k].y;}ph=f->runs[k].y-py+12;count++;}
                    place(t,i,px,py,ph,begin,count);
                }
                cy+=h;a=next;
            }
            if(cy-y+2.5f>row_height)row_height=cy-y+2.5f;
        }
        for(unsigned i=first_cell;i<t->count;i++)t->cells[i].h=row_height;
        y+=row_height;
    }
    *height=y-top;return end;
}
