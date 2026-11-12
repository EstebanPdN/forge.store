#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;typedef uint64_t u64;typedef int Result;typedef int Handle;typedef struct{int unused;}AM_TitleEntry;
#define R_FAILED(r) ((r)<0)
#define R_SUCCEEDED(r) ((r)>=0)
#define MEDIATYPE_SD 1
static bool homebrew,missing,bad_header;static int connect_rc,am_rc,fail_command,closes,chainloads,ipc_calls;
static const char *config;static u64 launched,title_checked;static char target[1024],argv_path[1024];
static FILE *fake_open(const char *path,const char *mode){
    (void)mode;FILE *f;
    if(!strcmp(path,"sdmc:/luma/config.ini")){if(!config)return NULL;f=tmpfile();assert(f);fputs(config,f);rewind(f);return f;}
    if(missing)return NULL;f=tmpfile();assert(f);char bytes[32]={0};memcpy(bytes,bad_header?"BAD!":"3DSX",4);fwrite(bytes,1,32,f);rewind(f);return f;
}
static bool envIsHomebrew(void){return homebrew;}
static Result AM_GetTitleInfo(int medium,int count,const u64 *title,AM_TitleEntry *entry){assert(medium==1&&count==1&&entry);title_checked=*title;return am_rc;}
static Result svcConnectToPort(Handle *port,const char *name){assert(!strcmp(name,"hb:ldr"));*port=17;return connect_rc;}
static void svcCloseHandle(Handle port){assert(port==17);closes++;}
static void aptSetChainloader(u64 title,int medium){assert(medium==1);launched=title;chainloads++;}
static Result hbl_transport(Handle port,unsigned command,unsigned descriptor,const void *data,size_t size){
    assert(port==17);ipc_calls++;if((int)command==fail_command)return -1;
    if(command==3){assert(descriptor==1&&size==1024&&*(const u32 *)data==1);snprintf(argv_path,sizeof argv_path,"%s",(const char *)data+4);}
    else{assert(command==2&&descriptor==0&&size==strlen(data)+1);snprintf(target,sizeof target,"%s",(const char *)data);}
    return 0;
}
#define FORGE_HBL_HOST_TEST
#define fopen fake_open
#include "../source/hbl_launch.c"
#undef fopen
static void reset(void){homebrew=true;missing=bad_header=false;connect_rc=am_rc=fail_command=closes=chainloads=ipc_calls=0;config=NULL;launched=title_checked=0;target[0]=argv_path[0]=0;}
int main(void){
    const char *path="sdmc:/3ds/My App/game.3dsx";reset();assert(hbl_launch(path)==HBL_LAUNCH_OK&&closes==1&&chainloads==0&&ipc_calls==2);assert(!strcmp(target,"/3ds/My App/game.3dsx")&&!strcmp(argv_path,path));
    reset();homebrew=false;assert(hbl_launch(path)==HBL_LAUNCH_OK&&chainloads==1&&launched==0x000400000d921e00ULL&&title_checked==launched);
    reset();homebrew=false;config="[boot]\nhbldr_3dsx_titleid = 0004000001234500\n";assert(hbl_launch(path)==HBL_LAUNCH_OK&&launched==0x0004000001234500ULL);
    const char *invalid[]={"hbldr_3dsx_titleid=garbage","hbldr_3dsx_titleid=0004013000001302","hbldr_3dsx_titleid=0004000001234500junk","hbldr_3dsx_titleid=123"};
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++){reset();homebrew=false;config=invalid[i];assert(hbl_launch(path)==HBL_LAUNCH_NO_TITLE&&!chainloads&&!ipc_calls);}
    reset();connect_rc=-1;assert(hbl_launch(path)==HBL_LAUNCH_UNAVAILABLE&&!closes&&!chainloads);
    reset();homebrew=false;am_rc=-1;assert(hbl_launch(path)==HBL_LAUNCH_NO_TITLE&&!ipc_calls);
    reset();fail_command=2;assert(hbl_launch(path)==HBL_LAUNCH_IPC&&closes==1&&!chainloads&&!strcmp(argv_path,"sdmc:/boot.3dsx"));
    reset();fail_command=3;assert(hbl_launch(path)==HBL_LAUNCH_IPC&&closes==1&&!chainloads&&!*target);
    reset();bad_header=true;assert(hbl_launch(path)==HBL_LAUNCH_BAD_FILE&&!ipc_calls);
    reset();missing=true;assert(hbl_launch(path)==HBL_LAUNCH_BAD_FILE);
    reset();assert(hbl_launch(NULL)==HBL_LAUNCH_BAD_FILE&&hbl_launch("https://game.3dsx")==HBL_LAUNCH_BAD_FILE&&hbl_launch("sdmc:/3ds/../game.3dsx")==HBL_LAUNCH_BAD_FILE&&hbl_launch("sdmc:/game.cia")==HBL_LAUNCH_BAD_FILE);
    puts("PASS: Rosalina 3DSX launch, CIA HBL chainloader, configured title, argv, service failures, bad files, cleanup");
}
