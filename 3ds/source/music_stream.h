#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

// Lossless, sequential PCM stream. Only the music-reader worker touches it.
// A gzip CRC failure or a truncated/empty loop stops the stream rather than spins.
typedef struct {
    gzFile file; const char *loop; bool opening;
    uint32_t remaining;
    uint16_t previous[2],older[2];
} MusicStream;
static inline void music_stream_close(MusicStream *s) {
    if (s->file) gzclose(s->file);
    s->file=NULL;
}
static inline uint32_t music_u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline bool music_stream_header(MusicStream *s){
    unsigned char header[12];
    if(gzread(s->file,header,12)!=12||memcmp(header,"FPC2",4)||music_u32(header+8)!=32000)return false;
    s->remaining=music_u32(header+4);
    memset(s->previous,0,sizeof s->previous);memset(s->older,0,sizeof s->older);
    return s->remaining&&s->remaining<=32*1024*1024&&s->remaining%4==0;
}
static inline bool music_stream_open(MusicStream *s,const char *opening,const char *loop) {
    music_stream_close(s);
    s->loop=loop; s->opening=true; s->file=gzopen(opening,"rb");
    if (s->file) gzbuffer(s->file,32768);
    if(s->file&&music_stream_header(s))return true;
    music_stream_close(s);return false;
}
static inline bool music_stream_read(MusicStream *s,unsigned char *dst,size_t bytes) {
    if (!s->file || bytes>1024*1024 || bytes%4) return false;
    size_t filled=0;
    while (filled<bytes) {
        if(!s->remaining){
            if(s->opening){
                music_stream_close(s);s->file=gzopen(s->loop,"rb");s->opening=false;
                if(!s->file)return false;
                gzbuffer(s->file,32768);
            }else if(gzrewind(s->file)!=0)return false;
            if(!music_stream_header(s))return false;
        }
        unsigned want=bytes-filled;if(want>s->remaining)want=s->remaining;
        int n=gzread(s->file,dst+filled,want);
        int error=0; gzerror(s->file,&error);
        if (n!=(int)want || (error!=Z_OK && error!=Z_STREAM_END)) return false;
        for(unsigned i=0;i<want;i+=2){
            unsigned channel=(i/2)&1;unsigned char *p=dst+filled+i;
            uint16_t value=(uint16_t)((p[0]|(unsigned)p[1]<<8)+2u*s->previous[channel]-s->older[channel]);
            s->older[channel]=s->previous[channel];s->previous[channel]=value;
            p[0]=(unsigned char)value;p[1]=(unsigned char)(value>>8);
        }
        filled+=(size_t)n;
        s->remaining-=want;
        if(!s->remaining){
            unsigned char extra;int tail=gzread(s->file,&extra,1);gzerror(s->file,&error);
            if(tail!=0||!gzeof(s->file)||(error!=Z_OK&&error!=Z_STREAM_END))return false;
        }
    }
    return true;
}
