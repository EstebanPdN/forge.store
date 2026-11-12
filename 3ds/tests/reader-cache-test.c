#include "../source/reader_cache.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){
    char dir[]="/tmp/forge-reader-cache-XXXXXX";assert(mkdtemp(dir));char path[200];snprintf(path,sizeof path,"%s/cache",dir);
    const char data[]="{\"public\":true}";size_t n=999;char *body;
    assert(reader_cache_save(path,1000,data,strlen(data),1024));
    body=reader_cache_load(path,1001,3600,1024,&n);assert(body&&n==strlen(data)&&!strcmp(body,data));free(body);
    assert(!reader_cache_load(path,999,3600,1024,&n)&&n==0);
    assert(!reader_cache_load(path,4600,3600,1024,&n));
    assert(!reader_cache_load(path,1001,3600,3,&n));
    assert(!reader_cache_save(path,2000,data,strlen(data),3));
    body=reader_cache_load(path,1001,3600,1024,&n);assert(body);free(body);
    FILE *f=fopen(path,"r+b");assert(f);fseek(f,22,SEEK_SET);fputc('X',f);fclose(f);assert(!reader_cache_load(path,1001,3600,1024,&n));
    assert(reader_cache_save(path,1000,data,strlen(data),1024));f=fopen(path,"ab");fputc('x',f);fclose(f);assert(!reader_cache_load(path,1001,3600,1024,&n));
    f=fopen(path,"wb");fwrite("legacy JSON",1,11,f);fclose(f);assert(!reader_cache_load(path,1001,3600,1024,&n));
    remove(path);rmdir(dir);puts("PASS: cache TTL, future clock, size limit, CRC, trailing data, atomic replacement, legacy format");
}
