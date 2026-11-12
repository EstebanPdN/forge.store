// Pre-sized README art avoids repeating PNG/WebP/SVG work on the Old 3DS CPU.
// Public bundled images are immutable. Other decoded images have a bounded SD
// cache; the HTTP worker still validates the originals independently.
#ifdef FORGE_READER_IMAGE_HOST_TEST
#include "../tests/reader-image-host.h"
#else
#include "common.h"
#include <mbedtls/sha256.h>
#endif
#include "reader_image.h"
#include <zlib.h>
#include <dirent.h>
#include <sys/stat.h>
#define IMAGE_DIR DATA_DIR "/reader-images-v1"
#define IMAGE_LIMIT (376u * 240u * 4u)
static void image_path(const char *url, bool bundled, char *path, size_t cap) {
    unsigned char digest[32]; char hex[65];
    mbedtls_sha256_ret((const unsigned char *)url,strlen(url),digest,0);
    for(int i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",digest[i]);
    snprintf(path,cap,"%s/%s.rimg",bundled?"romfs:/reader-art":IMAGE_DIR,hex);
}
bool reader_image_bundled(const char *url) {
    char path[256];image_path(url,true,path,sizeof path);
    struct stat st;return stat(path,&st)==0 && S_ISREG(st.st_mode);
}
void reader_image_invalidate(const char *url) {
    char path[256];image_path(url,false,path,sizeof path);remove(path);
}
static unsigned get32(const unsigned char *p) {return p[0]|p[1]<<8|p[2]<<16|(unsigned)p[3]<<24;}
static void put32(unsigned char *p,unsigned v) {for(int i=0;i<4;i++){p[i]=v;v>>=8;}}
static bool read_image(const char *path,unsigned char **rgba,int *w,int *h) {
    FILE *f=fopen(path,"rb");if(!f) return false;
    unsigned char head[20],*z=NULL,*pixels=NULL;bool ok=false;
    if(fread(head,1,20,f)!=20 || memcmp(head,"RIM1",4)) goto end;
    unsigned width=head[4]|head[5]<<8,height=head[6]|head[7]<<8;
    unsigned raw=get32(head+8),size=get32(head+12);
    if(!width||width>376||!height||height>240||raw!=width*height*4||!size||size>IMAGE_LIMIT+1024) goto end;
    z=malloc(size);pixels=malloc(raw);
    if(!z||!pixels||fread(z,1,size,f)!=size||fgetc(f)!=EOF||ferror(f)) goto end;
    uLongf length=raw;
    if(uncompress(pixels,&length,z,size)!=Z_OK||length!=raw||crc32(0,pixels,raw)!=get32(head+16)) goto end;
    *rgba=pixels;*w=width;*h=height;pixels=NULL;ok=true;
end:free(pixels);free(z);fclose(f);return ok;
}
bool reader_image_load(const char *url,unsigned char **rgba,int *w,int *h) {
    *rgba=NULL;*w=*h=0;char path[256];
    image_path(url,true,path,sizeof path);
    if(read_image(path,rgba,w,h)) return true;
    image_path(url,false,path,sizeof path);
    return read_image(path,rgba,w,h);
}
void reader_image_save(const char *url,const unsigned char *rgba,int w,int h) {
    if(!rgba||w<=0||w>376||h<=0||h>240) return;
    mkdir(IMAGE_DIR,0777);
    // At most 128 files (<=45 MiB worst case). Evict on the background worker.
    DIR *dir=opendir(IMAGE_DIR);struct dirent *e;unsigned count=0;char oldest[256]={0};time_t age=0;
    if(dir){while((e=readdir(dir))) if(strlen(e->d_name)==69 && !strcmp(e->d_name+64,".rimg")) {
        count++;char p[256];snprintf(p,sizeof p,IMAGE_DIR "/%.69s",e->d_name);struct stat st;
        if(!stat(p,&st) && (!oldest[0]||st.st_mtime<age)){age=st.st_mtime;snprintf(oldest,sizeof oldest,"%s",p);}
    }closedir(dir);}
    if(count>=128&&oldest[0]) remove(oldest);
    unsigned raw=w*h*4;uLongf size=compressBound(raw);unsigned char *z=malloc(size);
    if(!z) return;
    if(compress2(z,&size,rgba,raw,1)!=Z_OK){free(z);return;}
    unsigned char head[20]={ 'R','I','M','1',w,w>>8,h,h>>8 };
    put32(head+8,raw);put32(head+12,size);put32(head+16,crc32(0,rgba,raw));
    char path[256],temp[264];image_path(url,false,path,sizeof path);snprintf(temp,sizeof temp,"%s.tmp",path);
    FILE *f=fopen(temp,"wb");bool ok=false;
    if(f){ok=fwrite(head,1,20,f)==20&&fwrite(z,1,size,f)==size;if(fclose(f))ok=false;}
    if(!ok || rename(temp,path)) remove(temp);
    free(z);
}
