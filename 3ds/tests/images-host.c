#define REMOTE_IMAGE_HOST_TEST
#include "../source/remote_image.c"
#include <assert.h>
int main(int argc,char **argv){
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
    unsigned char *data=malloc(size);assert(data&&fread(data,1,size,f)==size);fclose(f);
    RIPixels pixels={0};assert(ri_decode(data,size,&pixels));
    assert(pixels.w>0&&pixels.w<=RI_WIDTH&&pixels.h>0&&pixels.h<=RI_HEIGHT&&pixels.rgba);
    printf("PASS: decoded %s -> %dx%d, bounded RGBA\n",argv[1],pixels.w,pixels.h);free(pixels.rgba);
    assert(!ri_decode(data,size>20?20:0,&(RIPixels){0}));
    // Mutated RIFF/WebP payloads exercise the actual bounded decoder wrapper.
    unsigned state=0x19383;
    for(int k=0;k<250;k++){
        unsigned char *mut=malloc(size);memcpy(mut,data,size);
        for(int i=0;i<8;i++){state=state*1664525+1013904223;mut[state%size]^=(unsigned char)(state>>16);}
        pixels=(RIPixels){0};if(ri_decode(mut,size,&pixels))assert(pixels.w<=RI_WIDTH&&pixels.h<=RI_HEIGHT);
        free(pixels.rgba);free(mut);
    }
    free(data);puts("PASS: truncated input and 250 payload mutations");
}
