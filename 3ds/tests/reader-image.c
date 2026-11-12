#define FORGE_READER_IMAGE_HOST_TEST
#include "../source/reader_image.c"
#include <assert.h>
int main(void) {
 const char*url="https://example.invalid/fixture.png";
 unsigned char pixels[64*32*4];for(size_t i=0;i<sizeof pixels;i++)pixels[i]=i%251;
 reader_image_save(url,pixels,64,32);unsigned char *loaded=NULL;int w,h;
 assert(reader_image_load(url,&loaded,&w,&h));assert(w==64&&h==32&&!memcmp(loaded,pixels,sizeof pixels));free(loaded);
 char path[256];image_path(url,false,path,sizeof path);
 FILE*f=fopen(path,"r+b");assert(f);fseek(f,16,SEEK_SET);fputc(0,f);fputc(0,f);fputc(0,f);fputc(0,f);fclose(f);
 assert(!reader_image_load(url,&loaded,&w,&h));assert(!loaded&&w==0&&h==0);
 reader_image_save(url,pixels,64,32);f=fopen(path,"ab");assert(f);fputc(1,f);fclose(f);assert(!reader_image_load(url,&loaded,&w,&h));
 reader_image_save(url,pixels,64,32);reader_image_invalidate(url);assert(!reader_image_load(url,&loaded,&w,&h));
 reader_image_save(url,pixels,10000,32);assert(!reader_image_load(url,&loaded,&w,&h));
 for(int i=0;i<132;i++){char u[64];snprintf(u,sizeof u,"https://example.invalid/%d",i);reader_image_save(u,pixels,64,32);}
 DIR*d=opendir(IMAGE_DIR);assert(d);struct dirent*e;int count=0;while((e=readdir(d)))if(strstr(e->d_name,".rimg"))count++;closedir(d);assert(count==128);
 puts("PASS decoded-image cache: roundtrip, CRC, trailing bytes, invalidation, dimensions, 128-file limit");
}
