#include "image_round.h"
#include <stdlib.h>
#include <math.h>

unsigned char *image_round_rgba(const unsigned char *source,int width,int height,int side,int radius){
    if(!source||width<1||width>376||height<1||height>240||side<1||side>128||radius<0||radius>side/2)return NULL;
    unsigned char *out=malloc((size_t)side*side*4);if(!out)return NULL;
    float scale=fmaxf((float)side/width,(float)side/height),half=side*.5f;
    for(int y=0;y<side;y++)for(int x=0;x<side;x++){
        float sx=(x+.5f-half)/scale+width*.5f-.5f,sy=(y+.5f-half)/scale+height*.5f-.5f;
        sx=fminf(width-1,fmaxf(0,sx));sy=fminf(height-1,fmaxf(0,sy));
        int ix=(int)floorf(sx),iy=(int)floorf(sy),nx=ix+1<width?ix+1:ix,ny=iy+1<height?iy+1:iy;
        const unsigned char *samples[]={source+(iy*width+ix)*4,source+(iy*width+nx)*4,source+(ny*width+ix)*4,source+(ny*width+nx)*4};
        float fx=sx-ix,fy=sy-iy,weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy},alpha=0,rgb[3]={0};
        for(int s=0;s<4;s++){float a=samples[s][3]*weights[s];alpha+=a;for(int c=0;c<3;c++)rgb[c]+=samples[s][c]*a;}
        float qx=fabsf(x+.5f-half)-(half-radius),qy=fabsf(y+.5f-half)-(half-radius);
        float dx=fmaxf(qx,0),dy=fmaxf(qy,0),distance=sqrtf(dx*dx+dy*dy)+fminf(fmaxf(qx,qy),0)-radius;
        float cover=radius?fminf(1,fmaxf(0,.5f-distance)):1;
        unsigned char *dest=out+(y*side+x)*4;
        for(int c=0;c<3;c++)dest[c]=alpha>0?(unsigned char)fminf(255,floorf(rgb[c]/alpha+.5f)):0;
        dest[3]=(unsigned char)fminf(255,floorf(alpha*cover+.5f));
    }
    return out;
}
