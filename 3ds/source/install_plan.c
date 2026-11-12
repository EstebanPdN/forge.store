#include "install_plan.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
static const char *str(json_t *j,const char *key){const char*s=json_string_value(json_object_get(j,key));return s?s:"";}
static bool path(char *out,const char *src){
 if(!src||!*src||strstr(src,"%TMP%"))return false;
 int n=snprintf(out,PLAN_PATH,"%s%s",!strncmp(src,"%3DSX%",6)?"/3ds":"",!strncmp(src,"%3DSX%",6)?src+6:src);
 if(n<=0||n>=PLAN_PATH||out[0]!='/'||strchr(out,'%'))return false;
 char *r=out,*w=out;while(*r){if(*r=='/'&&r[1]=='/'){r++;continue;}*w++=*r++;}*w=0;
 while(w>out+1&&w[-1]=='/')*--w=0;
 return strcmp(out,"/")!=0;
}
static int find(InstallPlan*p,const char*s){for(int i=0;i<p->count;i++)if(!strcmp(p->rows[i].path,s))return i;return -1;}
static void drop(InstallPlan*p,const char*s){int i=find(p,s);if(i>=0){memmove(p->rows+i,p->rows+i+1,(--p->count-i)*sizeof p->rows[0]);}}
static void put(InstallPlan*p,const char*s,bool dir,double size,int req,bool dsp){
 char name[PLAN_PATH];if(!path(name,s))return;int i=find(p,name);
 if(i<0){if(p->count==PLAN_MAX){p->truncated=true;return;}i=p->count++;}
 PlanEntry *e=&p->rows[i];memset(e,0,sizeof *e);strcpy(e->path,name);e->dir=dir;e->size=size;e->requirement=req;e->dsp=dsp;
 for(const char*c=name;*c;c++)if(*c=='/')e->depth++;
}
static int order(const void*a,const void*b){
 // Compare at the first distinct path component: a directory subtree precedes files.
 const PlanEntry *x=a,*y=b;const char *u=x->path+1,*v=y->path+1;
 for(;;){const char *ue=strchr(u,'/'),*ve=strchr(v,'/');size_t un=ue?(size_t)(ue-u):strlen(u),vn=ve?(size_t)(ve-v):strlen(v);
 int cmp=strncasecmp(u,v,un<vn?un:vn);if(!cmp&&un!=vn)cmp=un<vn?-1:1;
 if(cmp){bool ud=ue||x->dir,vd=ve||y->dir;return ud!=vd?(ud?-1:1):cmp;}
 if(!ue||!ve)return ue?1:ve?-1:strcmp(x->path,y->path);
 u=ue+1;v=ve+1;}
}
void install_plan_build(InstallPlan*p,json_t*steps,json_t*assets,json_t*requires,bool old3ds){
 memset(p,0,sizeof *p);size_t i;json_t*s;
 json_array_foreach(steps,i,s){const char*t=str(s,"type");
  if(!strcmp(t,"mkdir"))put(p,str(s,"directory"),true,0,-1,false);
  else if(!strcmp(t,"downloadRelease")||!strcmp(t,"downloadUrl")){
   json_t*a=!strcmp(t,"downloadRelease")?json_object_get(assets,str(s,"asset")):s;
   if(old3ds&&json_is_object(json_object_get(a,"o3ds")))a=json_object_get(a,"o3ds");
   put(p,str(s,"output"),false,json_number_value(json_object_get(a,"size")),-1,false);
  }else if(!strcmp(t,"move")){char from[PLAN_PATH];double size=0;if(path(from,str(s,"old"))){int j=find(p,from);if(j>=0)size=p->rows[j].size;drop(p,from);}put(p,str(s,"new"),false,size,-1,false);}
  else if(!strcmp(t,"deleteFile")){char name[PLAN_PATH];if(path(name,str(s,"file")))drop(p,name);}
  else if(!strcmp(t,"extractFile")){size_t k;json_t*c;json_array_foreach(json_object_get(s,"creates"),k,c)put(p,str(c,"path"),false,json_number_value(json_object_get(c,"size")),-1,false);}
  else if(!strcmp(t,"dumpDsp"))put(p,"/3ds/dspfirm.cdc",false,0,-1,true);
  else if(!strcmp(t,"installCia"))p->cia=true;
 }
 json_array_foreach(requires,i,s){const char*name=str(s,"path");char full[PLAN_PATH];
  if(*name&&name[strlen(name)-1]=='/'&&*str(s,"example")){int n=snprintf(full,sizeof full,"%s%s",name,str(s,"example"));if(n>0&&n<PLAN_PATH)name=full;}
  put(p,name,false,json_number_value(json_object_get(s,"size")),i,false);
 }
 int originals=p->count;
 for(int i=0;i<originals;i++){char parent[PLAN_PATH];strcpy(parent,p->rows[i].path);char *slash;
  while((slash=strrchr(parent,'/'))&&slash!=parent){*slash=0;if(find(p,parent)<0)put(p,parent,true,0,-1,false);}
 }
 qsort(p->rows,p->count,sizeof p->rows[0],order);
}
