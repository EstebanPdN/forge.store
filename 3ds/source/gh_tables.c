#include "gh_tables.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static bool separator(const char *s,const char *end){unsigned dash=0;for(;s<end;s++){if(*s=='-')dash++;else if(*s!='|'&&*s!=':'&&!isspace((unsigned char)*s))return false;}return dash>=3;}
static bool pipe(const char *s,const char *end){const char *begin=s;for(;s<end;s++)if(*s=='|'&&(s==begin||s[-1]!='\\'))return true;return false;}
typedef struct {char *text;unsigned char *styles;size_t length,limit;bool *cut;} Writer;
static void put(Writer*w,const char*s,const unsigned char*styles,size_t n){if(n>w->limit-w->length){n=w->limit-w->length;*w->cut=true;}memcpy(w->text+w->length,s,n);if(styles)memcpy(w->styles+w->length,styles,n);w->length+=n;w->text[w->length]=0;}
static void marker(Writer*w,char c){char m[]={31,c,'\n'};put(w,m,NULL,3);}
static void row(Writer*w,const char *s,const char *end,const unsigned char *styles,bool header){
    marker(w,'R');while(s<end&&isspace((unsigned char)*s)){s++;styles++;}if(s<end&&*s=='|'){s++;styles++;}
    while(end>s&&isspace((unsigned char)end[-1]))end--;
    if(end>s&&end[-1]=='|'&&(end-s<2||end[-2]!='\\'))end--;
    for(const char *at=s;at<=end;){const char *next=at;while(next<end&&(*next!='|'||(next>at&&next[-1]=='\\')))next++;
        marker(w,header?'H':'C');put(w,at,styles+(at-s),next-at);put(w,"\n",NULL,1);if(next==end)break;at=next+1;}
}
void gh_tables_project(char **plain,unsigned char **styles,size_t *length,bool *cut,size_t limit){
    char *out=calloc(limit+1,1);unsigned char *st=calloc(limit+1,1);if(!out||!st){free(out);free(st);*cut=true;return;}
    Writer w={out,st,0,limit,cut};bool table=false,html=false,code=false;char *begin=*plain,*line=begin;
    while(*line&&w.length<limit){char *end=strchr(line,'\n');if(!end)end=line+strlen(line);char *next=*end?end+1:end,*after=strchr(next,'\n');if(!after)after=next+strlen(next);
        const char *trim=line;while(trim<end&&isspace((unsigned char)*trim))trim++;
        bool fence=end-trim>=3&&(!strncmp(trim,"```",3)||!strncmp(trim,"~~~",3));if(fence)code=!code;
        if(end-line>=2&&line[0]==31){if(line[1]=='T')html=true;else if(line[1]=='E')html=false;}
        if(!html&&!code&&!fence&&!table&&pipe(line,end)&&separator(next,after)){
            marker(&w,'T');row(&w,line,end,*styles+(line-begin),true);table=true;line=*after?after+1:after;continue;
        }
        if(table){if(pipe(line,end)&&end>line){row(&w,line,end,*styles+(line-begin),false);line=next;continue;}marker(&w,'E');table=false;}
        put(&w,line,*styles+(line-begin),end-line);if(*end)put(&w,"\n",*styles+(end-begin),1);line=next;
    }
    if(table)marker(&w,'E');
    if(*line)*cut=true;
    free(*plain);free(*styles);*plain=out;*styles=st;*length=w.length;
}
