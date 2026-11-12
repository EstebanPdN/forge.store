// Real Sheet and production queue/decoder; SDK textures/events are host adapters.
#include "gfx.h"
#include <assert.h>
#include <zlib.h>
#include <unistd.h>
static _Atomic bool gate;
void bg_wait(void){while(gate)usleep(100);}
#include "../source/texture_stream.inc"
static void *run_loader(void*p){loader_main(p);return NULL;}
static void drain(void){
 for(int i=0;i<10000&&npending;i++){sheets_pump();usleep(100);}
 assert(!npending);
}
int main(void){
 LightLock_Init(&q_lock);LightEvent_Init(&q_event,0);LightEvent_Init(&upload_event,0);
 size_t n;unsigned char*bytes=sheet_file("sheet.ftx",&n,1024*1024);assert(bytes);
 Sheet s={0};assert(decode_bytes(&s,bytes,n));assert(s.size==8*8*4);sheet_free(&s);
 for(size_t i=0;i<n;i++)assert(!decode_bytes(&s,bytes,i));
 unsigned char saved=bytes[8];bytes[8]=5;assert(!decode_bytes(&s,bytes,n));bytes[8]=saved;
 saved=bytes[4];bytes[4]=7;assert(!decode_bytes(&s,bytes,n));bytes[4]=saved;
 saved=bytes[16];bytes[16]=9;assert(!decode_bytes(&s,bytes,n));bytes[16]=saved;
 const unsigned char*bank=bank_entry("romfs:/gfx/banner/demo-bg.ftx",&n);assert(bank&&decode_bytes(&s,bank,n));sheet_free(&s);
 assert(!bank_entry("romfs:/gfx/banner/missing-bg.ftx",&n));
 unsigned at=read32(banner_bank+12+64);memset(banner_bank+12+64,255,4);
 assert(!bank_entry("romfs:/gfx/banner/demo-bg.ftx",&n));memcpy(banner_bank+12+64,&at,4);
 Sheet a={0},b={0},c={0};
 sheet_prefetch(&a,"sheet.ftx");sheet_prefetch(&b,"sheet.ftx");sheet_request(&c,"sheet.ftx");
 assert(nqueue==3&&queue[0]==&c&&queue[1]==&a&&queue[2]==&b);
 sheet_request(&b,"sheet.ftx");assert(queue[0]==&b&&nqueue==3&&npending==3);
 sheet_free(&a);assert(a.discard);sheet_request(&a,"sheet.ftx");assert(!a.discard&&npending==3);
 pthread_t thread;gate=true;assert(!pthread_create(&thread,NULL,run_loader,NULL));
 usleep(2000);sheet_free(&a);gate=false;drain();
 assert(a.state==SHEET_EMPTY&&b.state==SHEET_READY&&c.state==SHEET_READY);
 assert(!a.loads&&b.loads==1&&c.loads==1);
 sheet_request(&b,"sheet.ftx");assert(!npending&&b.loads==1);
 sheet_free(&b);sheet_request(&b,"sheet.ftx");drain();assert(b.loads==2);
 sheet_free(&b);sheet_free(&c);
 // Publish decoded pixels, cancel before upload, then immediately re-request.
 sheet_request(&a,"sheet.ftx");
 for(int i=0;i<10000&&a.state==SHEET_QUEUED;i++)usleep(100);
 assert(a.state==SHEET_DECODED);sheet_free(&a);sheet_request(&a,"sheet.ftx");drain();
 assert(a.state==SHEET_READY&&a.loads==1);sheet_free(&a);
 loader_quit=true;LightEvent_Signal(&q_event);LightEvent_Signal(&upload_event);pthread_join(thread,NULL);
 // Queue overflow and repeated promotion must not duplicate pointers.
 Sheet many[QMAX+1]={0};
 for(int i=0;i<QMAX+1;i++)sheet_prefetch(&many[i],"sheet.ftx");
 assert(nqueue==QMAX&&npending==QMAX&&many[QMAX].state==SHEET_EMPTY);
 for(int i=0;i<QMAX;i++)sheet_request(&many[i],"sheet.ftx");
 assert(nqueue==QMAX&&npending==QMAX);
 loader_quit=false;assert(!pthread_create(&thread,NULL,run_loader,NULL));drain();
 for(int i=0;i<QMAX;i++){assert(many[i].state==SHEET_READY&&many[i].loads==1);sheet_free(&many[i]);}
 loader_quit=true;LightEvent_Signal(&q_event);LightEvent_Signal(&upload_event);pthread_join(thread,NULL);
 free(bytes);free(banner_bank);
 puts("PASS: production FTX/bank bounds, priority, cancellation before/after decode, reacquire, queue overflow and shutdown");
}
