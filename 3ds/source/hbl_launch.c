// Rosalina's public hb:ldr IPC, as used by devkitPro/3ds-hbmenu.
// A 3DSX returns to its loader; a CIA chainloads the configured HBL title.
#include "hbl_launch.h"
#ifndef FORGE_HBL_HOST_TEST
#include <3ds.h>
#endif
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FORGE_HBL_HOST_TEST
static Result hbl_transport(Handle port,unsigned command,unsigned descriptor,const void *data,size_t size){
    u32 *ipc=getThreadCommandBuffer();ipc[0]=IPC_MakeHeader(command,0,2);
    ipc[1]=IPC_Desc_StaticBuffer(size,descriptor);ipc[2]=(u32)data;
    Result result=svcSendSyncRequest(port);return R_FAILED(result)?result:(Result)ipc[1];
}
#endif
static u64 hbl_title(void){
    u64 title=0x000400000d921e00ULL;
    FILE *f=fopen("sdmc:/luma/config.ini","r");if(!f)return title;
    char line[256];
    while(fgets(line,sizeof line,f)){
        char *s=line;while(isspace((unsigned char)*s))s++;
        const char key[]="hbldr_3dsx_titleid";if(strncmp(s,key,sizeof key-1))continue;
        s+=sizeof key-1;while(isspace((unsigned char)*s))s++;if(*s++!='=')continue;
        while(isspace((unsigned char)*s))s++;
        char *end;unsigned long long value=strtoull(s,&end,16);
        if(end==s||end-s!=16){title=0;break;}while(isspace((unsigned char)*end))end++;
        if(*end&&*end!='#'&&*end!=';'){title=0;break;}title=value;break;
    }fclose(f);
    return (title>>32)==0x00040000?title:0;
}
HblLaunchResult hbl_launch(const char *path){
    if(!path||strncmp(path,"sdmc:/",6)||strlen(path)>1018||strstr(path,"/../"))return HBL_LAUNCH_BAD_FILE;
    size_t n=strlen(path);if(n<5||strcmp(path+n-5,".3dsx"))return HBL_LAUNCH_BAD_FILE;
    unsigned char header[32];FILE *f=fopen(path,"rb");if(!f)return HBL_LAUNCH_BAD_FILE;
    bool valid=fread(header,1,sizeof header,f)==sizeof header&&!memcmp(header,"3DSX",4);fclose(f);
    if(!valid)return HBL_LAUNCH_BAD_FILE;
    bool homebrew=envIsHomebrew();u64 title=0;
    if(!homebrew){title=hbl_title();AM_TitleEntry entry;if(!title||R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD,1,&title,&entry)))return HBL_LAUNCH_NO_TITLE;}
    Handle port;Result result=svcConnectToPort(&port,"hb:ldr");if(R_FAILED(result))return HBL_LAUNCH_UNAVAILABLE;
    u32 arguments[256]={1};memcpy((char *)arguments+4,path,n+1);
    result=hbl_transport(port,3,1,arguments,sizeof arguments);
    if(R_SUCCEEDED(result))result=hbl_transport(port,2,0,path+5,n-4);
    if(R_FAILED(result)){
        u32 fallback[256]={1};strcpy((char *)fallback+4,"sdmc:/boot.3dsx");
        hbl_transport(port,3,1,fallback,sizeof fallback);
        svcCloseHandle(port);return HBL_LAUNCH_IPC;
    }
    svcCloseHandle(port);
    if(!homebrew)aptSetChainloader(title,MEDIATYPE_SD);
    return HBL_LAUNCH_OK;
}
