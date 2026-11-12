#include "reader_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
static uint64_t get(const unsigned char *p,int n){uint64_t v=0;for(int i=n-1;i>=0;i--)v=(v<<8)|p[i];return v;}
static void put(unsigned char *p,uint64_t v,int n){for(int i=0;i<n;i++){p[i]=(unsigned char)v;v>>=8;}}
char *reader_cache_load(const char *path,uint64_t now,uint64_t ttl,size_t limit,size_t *length){
    if(length)*length=0;
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    unsigned char header[20];char *body=NULL;
    if(fread(header,1,sizeof header,f)!=sizeof header||memcmp(header,"FGH2",4))goto end;
    uint64_t saved=get(header+4,8);size_t n=(size_t)get(header+12,4);
    if(now<saved||now-saved>=ttl||!n||n>limit||n==SIZE_MAX)goto end;
    body=malloc(n+1);if(!body)goto end;
    if(fread(body,1,n,f)!=n||fgetc(f)!=EOF||ferror(f)||crc32(0,(unsigned char *)body,n)!=(uint32_t)get(header+16,4)){
        free(body);body=NULL;goto end;
    }
    body[n]=0;if(length)*length=n;
end:fclose(f);return body;
}
bool reader_cache_save(const char *path,uint64_t now,const char *body,size_t length,size_t limit){
    if(!body||!length||length>limit||length>UINT32_MAX||strlen(path)>400)return false;
    unsigned char header[20];memcpy(header,"FGH2",4);put(header+4,now,8);put(header+12,length,4);put(header+16,crc32(0,(const unsigned char *)body,length),4);
    char temp[410];snprintf(temp,sizeof temp,"%s.tmp",path);FILE *f=fopen(temp,"wb");if(!f)return false;
    bool ok=fwrite(header,1,sizeof header,f)==sizeof header&&fwrite(body,1,length,f)==length;
    if(fclose(f))ok=false;
    if(ok && rename(temp,path)!=0){
        // libctru/FAT cannot overwrite an existing destination. Retain the old
        // complete cache until its replacement is ready, then restore on failure.
        char backup[414];snprintf(backup,sizeof backup,"%s.bak",path);
        remove(backup);
        if(rename(path,backup)!=0)ok=false;
        else if(rename(temp,path)!=0){rename(backup,path);ok=false;}
        else remove(backup);
    }
    if(!ok)remove(temp);
    return ok;
}
