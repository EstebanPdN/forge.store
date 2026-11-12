// Main thread owns requests/uploads/frees. The worker owns decode only, publishing
// pixels through atomic state. At most one decoded sheet waits for GPU upload.
#define QMAX 128
#define BANK_LIMIT (8u*1024u*1024u)
static Sheet *pending[QMAX], *queue[QMAX];
static int npending, nqueue;
static LightLock q_lock;
static LightEvent q_event, upload_event;
static Thread loader;
static _Atomic bool loader_quit;
static unsigned char *banner_bank;
static size_t banner_bank_size;
static bool bank_attempted;
static unsigned read16(const unsigned char *p){return p[0]|p[1]<<8;}
static unsigned read32(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}

static unsigned char *sheet_file(const char *path,size_t *size,size_t limit){
    *size=0;FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);rewind(f);
    if(n<=0||(size_t)n>limit){fclose(f);return NULL;}
    unsigned char *data=malloc(n);
    if(data&&(fread(data,1,n,f)!=(size_t)n||ferror(f))){free(data);data=NULL;}
    fclose(f);if(data)*size=(size_t)n;return data;
}
static const unsigned char *bank_entry(const char *path,size_t *size){
    const char *prefix="romfs:/gfx/banner/";
    if(strncmp(path,prefix,strlen(prefix)))return NULL;
    if(!bank_attempted){
        bank_attempted=true;
        banner_bank=sheet_file("romfs:/gfx/banner-bank.bin",&banner_bank_size,BANK_LIMIT);
        if(banner_bank&&(banner_bank_size<12||memcmp(banner_bank,"FBN1",4)||read32(banner_bank+8)!=banner_bank_size||
           read32(banner_bank+4)>256||12u+72u*read32(banner_bank+4)>banner_bank_size)){
            free(banner_bank);banner_bank=NULL;banner_bank_size=0;
        }
    }
    if(!banner_bank)return NULL;
    unsigned count=read32(banner_bank+4),start=12+count*72;
    for(unsigned i=0;i<count;i++){
        const unsigned char *entry=banner_bank+12+i*72;
        if(!memchr(entry,0,64)||strcmp((const char*)entry,path+strlen(prefix)))continue;
        unsigned at=read32(entry+64),n=read32(entry+68);
        if(at<start||at>banner_bank_size||n>banner_bank_size-at)return NULL;
        *size=n;return banner_bank+at;
    }
    return NULL;
}
static bool decode_bytes(Sheet *s,const unsigned char *data,size_t length){
    if(length<20||memcmp(data,"FTX1",4))return false;
    unsigned w=read16(data+4),h=read16(data+6),fmt=data[8],count=data[9];
    static const unsigned bpp[]={4,2,2,1,3};
    if(w<8||w>1024||(w&(w-1))||h<8||h>1024||(h&(h-1))||fmt>4||count>SHEET_MAX)return false;
    unsigned at=12+count*8;if(at+8>length)return false;
    unsigned raw=read32(data+at),packed=read32(data+at+4);
    if(raw!=w*h*bpp[fmt]||packed!=length-at-8)return false;
    for(unsigned i=0;i<count;i++){
        const unsigned char *r=data+12+i*8;
        unsigned x=read16(r),y=read16(r+2),rw=read16(r+4),rh=read16(r+6);
        if(!rw||!rh||x+rw>w||y+rh>h)return false;
        s->sub[i]=(Tex3DS_SubTexture){.width=rw,.height=rh,.left=x/(float)w,.right=(x+rw)/(float)w,
            .top=1-y/(float)h,.bottom=1-(y+rh)/(float)h};
    }
    void *pixels=malloc(raw);if(!pixels)return false;
    uLongf out=raw;bool ok=uncompress(pixels,&out,data+at+8,packed)==Z_OK&&out==raw;
    if(!ok){free(pixels);return false;}
    s->w=w;s->h=h;s->fmt=fmt;s->count=count;s->pixels=pixels;s->size=raw;return true;
}
static bool decode(Sheet *s,bool use_bank){
    u64 start=osGetTime();size_t n=0;unsigned char *owned=NULL;
    const unsigned char *data=use_bank?bank_entry(s->path,&n):NULL;
    if(!data)data=owned=sheet_file(s->path,&n,4u*1024u*1024u+1024);
    bool ok=data&&decode_bytes(s,data,n);free(owned);
    s->loads++;s->decode_ms=(float)(osGetTime()-start);return ok;
}
static GPU_TEXCOLOR tex_fmt(Sheet *s){
    static const GPU_TEXCOLOR f[]={GPU_RGBA8,GPU_RGB565,GPU_RGBA4,GPU_A8,GPU_RGB8};return f[s->fmt];
}
static bool upload_part(Sheet *s,size_t budget){
    if(!s->tex_init){
        if(!C3D_TexInit(&s->tex,s->w,s->h,tex_fmt(s))){free(s->pixels);s->pixels=NULL;s->state=SHEET_FAILED;return false;}
        C3D_TexSetFilter(&s->tex,s->crisp?GPU_NEAREST:GPU_LINEAR,GPU_LINEAR);
        C3D_TexSetWrap(&s->tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        s->tex_init=true;s->copied=0;
    }
    size_t n=s->size-s->copied;if(n>budget)n=budget;
    memcpy((u8*)s->tex.data+s->copied,(u8*)s->pixels+s->copied,n);s->copied+=n;
    if(s->copied<s->size)return false;
    C3D_TexFlush(&s->tex);free(s->pixels);s->pixels=NULL;return true;
}
bool sheet_load_now(Sheet *s,const char *path){
    snprintf(s->path,sizeof s->path,"%s",path);s->tex_init=false;
    bool ok=decode(s,false)&&upload_part(s,SIZE_MAX);s->state=ok?SHEET_READY:SHEET_FAILED;return ok;
}
static void loader_main(void *arg){
    (void)arg;
    while(!loader_quit){
        LightEvent_Wait(&q_event);
        for(;;){
            LightLock_Lock(&q_lock);Sheet *s=nqueue?queue[0]:NULL;
            if(s){memmove(queue,queue+1,(--nqueue)*sizeof *queue);}
            LightLock_Unlock(&q_lock);
            if(!s||loader_quit)break;
            bg_wait();
            // A cancelled request remains tracked until the main-thread pump.
            bool ok=!s->discard&&decode(s,true);
            s->state=ok?SHEET_DECODED:SHEET_FAILED;
            while(!loader_quit&&s->state==SHEET_DECODED)LightEvent_Wait(&upload_event);
        }
    }
}
static void request(Sheet *s,const char *path,bool urgent){
    if(!s||!path||strlen(path)>=sizeof s->path)return;
    if(s->state==SHEET_QUEUED||s->state==SHEET_DECODED)s->discard=false;
    if(s->state==SHEET_QUEUED&&urgent){
        LightLock_Lock(&q_lock);
        for(int i=1;i<nqueue;i++)if(queue[i]==s){memmove(queue+1,queue,i*sizeof *queue);queue[0]=s;break;}
        LightLock_Unlock(&q_lock);return;
    }
    if(s->state!=SHEET_EMPTY||npending>=QMAX)return;
    LightLock_Lock(&q_lock);
    if(nqueue>=QMAX){LightLock_Unlock(&q_lock);return;}
    snprintf(s->path,sizeof s->path,"%s",path);s->tex_init=false;s->copied=0;s->discard=false;
    s->state=SHEET_QUEUED;
    if(urgent){memmove(queue+1,queue,nqueue*sizeof *queue);queue[0]=s;}else queue[nqueue]=s;
    nqueue++;pending[npending++]=s;LightLock_Unlock(&q_lock);LightEvent_Signal(&q_event);
}
void sheet_request(Sheet *s,const char *path){request(s,path,true);}
void sheet_prefetch(Sheet *s,const char *path){request(s,path,false);}
#define PUMP_BUDGET (384u*1024u)
void sheets_pump(void){
    size_t budget=PUMP_BUDGET;
    for(int i=0;i<npending;i++){
        Sheet *s=pending[i];SheetState state=s->state;
        if((state==SHEET_DECODED||state==SHEET_FAILED)&&s->discard){
            if(s->tex_init)C3D_TexDelete(&s->tex);
            s->tex_init=false;
            free(s->pixels);s->pixels=NULL;s->discard=false;s->state=SHEET_EMPTY;
        }else if(state==SHEET_DECODED&&budget){
            size_t before=s->copied;if(upload_part(s,budget))s->state=SHEET_READY;
            size_t used=s->copied-before;budget=used>=budget?0:budget-used;
        }
        state=s->state;
        if(state==SHEET_READY||state==SHEET_FAILED||state==SHEET_EMPTY){
            pending[i--]=pending[--npending];LightEvent_Signal(&upload_event);
        }
    }
}
void sheet_free(Sheet *s){
    if(s->state==SHEET_QUEUED||s->state==SHEET_DECODED){s->discard=true;return;}
    for(int i=0;i<npending;i++)if(pending[i]==s){pending[i--]=pending[--npending];}
    if(s->tex_init)C3D_TexDelete(&s->tex);
            s->tex_init=false;
    free(s->pixels);s->pixels=NULL;s->size=0;s->discard=false;s->state=SHEET_EMPTY;
}
