#include "../source/image_round.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(void){
    unsigned char source[64*64*4];memset(source,255,sizeof source);
    const int sizes[]={16,34,46,128};
    for(unsigned s=0;s<sizeof sizes/sizeof sizes[0];s++){
        int n=sizes[s];unsigned char *p=image_round_rgba(source,64,64,n,n/2);assert(p);
        assert(p[3]==0&&p[((n/2)*n+n/2)*4+3]==255);
        unsigned partial=0;float area=0;
        for(int y=0;y<n;y++)for(int x=0;x<n;x++){
            unsigned a=p[(y*n+x)*4+3];area+=a/255.f;partial+=a>0&&a<255;
            assert(a==p[(y*n+(n-1-x))*4+3]&&a==p[((n-1-y)*n+x)*4+3]);
            assert(p[(y*n+x)*4]==255);
        }
        float expected=3.14159265f*n*n/4;assert(area>expected*.98f&&area<expected*1.02f&&partial>0);free(p);
    }
    unsigned char *p=image_round_rgba(source,64,64,34,9);assert(p&&p[3]==0&&p[(17*34)*4+3]==255);free(p);
    // Transparent source colors cannot bleed into a partly transparent sample.
    const unsigned char alpha[]={255,0,0,255, 0,0,255,0};
    p=image_round_rgba(alpha,2,1,3,0);assert(p);for(int i=0;i<9;i++)if(p[i*4+3])assert(p[i*4]==255&&p[i*4+2]==0);free(p);
    assert(!image_round_rgba(NULL,1,1,1,0));assert(!image_round_rgba(source,377,64,46,23));
    assert(!image_round_rgba(source,64,64,129,23)&&!image_round_rgba(source,64,64,34,18));
    puts("PASS: native avatar circle/rounded corners, antialias, symmetry, cover sampling, premultiplied transparency, bounds");
}
