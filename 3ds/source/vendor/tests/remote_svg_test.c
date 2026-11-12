#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../remote_svg.h"

static void check(const char *s, int valid) {
    unsigned char *rgba=(void *)(uintptr_t)1; int w=-1,h=-1;
    int ok=remote_svg_decode((const unsigned char *)s,strlen(s),&rgba,&w,&h);
    if(ok != valid) { fprintf(stderr,"Unexpected decode=%d: %s\n",ok,s); abort(); }
    if(ok) { assert(rgba && w>0 && w<=376 && h>0 && h<=240); free(rgba); }
    else assert(!rgba && !w && !h);
}
static void fixture(const char *path) {
    FILE *f=fopen(path,"rb"); assert(f); assert(!fseek(f,0,SEEK_END)); long size=ftell(f); rewind(f);
    unsigned char *data=malloc((size_t)size); assert(data); assert(fread(data,1,(size_t)size,f)==(size_t)size); fclose(f);
    unsigned char *rgba=NULL; int w=0,h=0;
    assert(remote_svg_decode(data,(size_t)size,&rgba,&w,&h)); assert(w==376 && h==185);
    /* Portrait and right-hand metadata must both contain rendered text. */
    unsigned different_left=0,different_right=0;
    for(int y=8;y<150;++y) for(int x=7;x<315;++x) {
        const unsigned char *p=rgba+((size_t)y*w+x)*4, *bg=rgba+((size_t)80*w+150)*4;
        if(memcmp(p,bg,3)) { if(x<130) ++different_left; if(x>132) ++different_right; }
    }
    assert(different_left>1000 && different_right>1000);
    char out[512]; snprintf(out,sizeof(out),"%s.ppm",path); f=fopen(out,"wb"); assert(f);
    fprintf(f,"P6\n%d %d\n255\n",w,h); for(int i=0;i<w*h;++i) fwrite(rgba+i*4,1,3,f); fclose(f);
    printf("PASS %s: %dx%d; portrait=%u metadata=%u\n",path,w,h,different_left,different_right);
    free(rgba); free(data);
}
int main(int argc,char **argv) {
    const char *valid[]={
        "<svg width='20' height='10'><rect width='20' height='10' fill='red'/></svg>",
        "<svg width='100' height='50'><g fill='rgb(20,30,40)' transform='translate(1,2) scale(1.5)'><text x='2' y='20' font-size='10' text-anchor='middle' opacity='0.5'> A&amp;B &lt; &#65;&#x42; </text></g></svg>",
        "<svg width='20' height='20'><path d='M 1 1 L 10 1 10 10 Z' fill='#abc'/></svg>",
        "<svg width='20' height='20'><circle cx='10' cy='10' r='8' fill='none' stroke='blue'/><ellipse cx='5' cy='5' rx='2' ry='1'/></svg>",
        "<svg width='20' height='10'><text y='8' font-size='8'>A</text><rect width='20' height='10' fill='blue'/></svg>",
        "<svg width='20' height='10'/>",
        "<svg width='20' height='10'><g font-family='Verdana,sans-serif'><text x='2' y='8' font-size='8'>A</text></g></svg>",
        "<svg width='20' height='10'><clipPath id='badge'><rect width='20' height='10' rx='3'/></clipPath><g clip-path='url(#badge)'><rect width='20' height='10' fill='#abc'/></g></svg>", NULL};
    const char *bad[]={
        "", "<svg>", "<svg width='20' height='20'>", "<svg width='20' height='20'></g>",
        "<!DOCTYPE svg [<!ENTITY a SYSTEM 'file:///etc/passwd'>]><svg width='20' height='20'/>",
        "<svg width='20' height='20'><image href='https://example.com/a.png'/></svg>",
        "<svg width='20' height='20'><use href='#a'/></svg>",
        "<svg width='20' height='10'><clipPath id='badge'><rect width='30' height='10' rx='3'/></clipPath></svg>",
        "<svg width='20' height='10'><clipPath id='badge'><rect width='20' height='10' rx='3'/></clipPath><g clip-path='url(https://example.com)'></g></svg>",
        "<svg width='20' height='20'><script>alert(1)</script></svg>",
        "<svg width='20' height='20'><foreignObject/></svg>",
        "<svg width='20' height='20'><text>&bogus;</text></svg>",
        "<svg width='20' height='20'><text>&#99999999999;</text></svg>",
        "<svg width='20' height='20'><text>á</text></svg>",
        "<svg width='20' height='20'><text><tspan>A</tspan></text></svg>",
        "<svg width='20' height='20'><rect fill='url(http://example.com)'/></svg>",
        "<svg width='20' height='20'><rect filter='blur(2)'/></svg>",
        "<svg width='20' height='20'><style>text {fill:red;}</style></svg>",
        "<svg width='1e30' height='20'/>", "<svg width='nan' height='20'/>",
        "<svg width='20' width='20' height='20'/>",
        "<svg width='20' height='20'><g opacity='0.5'><rect/></g></svg>",
        "<svg width='20' height='20'><text transform='rotate(45)'>A</text></svg>",
        "<svg width='20' height='20'><text transform='scale(1,2)'>A</text></svg>",
        "<svg width='20' height='20'><path d='M0 0 L'/></svg>",
        "<svg width='20' height='20'><path d='M 0 0 A 2 2 0 0 0 4 4'/></svg>",
        "<svg width='20' height='20'><rect width='1' height='1' fill='madeup'/></svg>",
        "<svg width='20' height='20'><rect width='1' height='1' fill='rgb(1,2,999)'/></svg>", NULL};
    for(int i=0;valid[i];++i) check(valid[i],1);
    for(int i=0;bad[i];++i) check(bad[i],0);
    /* Allocation cap / deep nesting, truncation, embedded NUL, enormous input. */
    char deep[1024]="<svg width='20' height='20'>"; for(int i=0;i<40;++i) strcat(deep,"<g>"); check(deep,0);
    unsigned char *out=NULL; int w,h; assert(!remote_svg_decode((const unsigned char *)"x\0x",3,&out,&w,&h));
    assert(!remote_svg_decode((const unsigned char *)"x",128*1024+1,&out,&w,&h));
    /* Deterministic mutations exercise the strict scanner without network. */
    const char *seed=valid[1]; size_t len=strlen(seed); uint32_t rng=42;
    for(int i=0;i<3000;++i) { char s[512]; strcpy(s,seed); rng=rng*1664525u+1013904223u;
        s[rng%len]=(char)(1+(rng>>16)%126); out=NULL;
        if(remote_svg_decode((unsigned char *)s,(i%3)?strlen(s):rng%len,&out,&w,&h)) free(out);
    }
    for(int i=1;i<argc;++i) fixture(argv[i]);
    puts("PASS SVG subset, rejection, limits, and 3000 mutations"); return 0;
}
