#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <jansson.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonCryptor.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int Result;
typedef int Handle;
typedef void *Thread;
typedef int LightLock;
typedef int LightEvent;
typedef struct { json_t *j; const char *id, *repo; bool old_ok; const char *folder; } App;
#define MAX_APPS 4
static App APPS[MAX_APPS];
static int NAPPS;
#define DATA_DIR "sdmc:/3ds/forge-store"
#define DANGER 1
#define AMBER 2
#define MINT 3
#define U64_MAX UINT64_MAX
#define CUR_THREAD_HANDLE 0
#define RESET_ONESHOT 0
#define SYSCLOCK_ARM11 268123480ULL
#define R_SUCCEEDED(r) ((r)>=0)
#define R_FAILED(r) ((r)<0)
#define MEDIATYPE_SD 1
#define ARCHIVE_SDMC 1
#define PATH_EMPTY 0
#define PATH_ASCII 1
#define FS_OPEN_READ 1
static bool IS_NEW3DS = true;
static struct {int lang;} S;
static u64 host_free_space = UINT64_MAX;
static bool host_commit_ok = true, host_auto_ack;
static float clampf(float v,float a,float b) {return v<a?a:v>b?b:v;}
#include "install-types.h"
static const char *T(const char *s) {return s;}
static const char *LJ(json_t *v) {const char *s=json_string_value(v);return s?s:"";}
static void toast(const char *s,u32 c) {(void)s;(void)c;}
static bool path_exists(const char *s);
static json_t *host_record;
static json_t *installed_record(App *a) {return host_record;}
// Simulated power loss: the process dies just before its Nth SD/AM mutation.
static long host_crash_at = -1, host_mutations;
static void host_mutation(void) {long n=host_mutations++;if (host_crash_at >= 0 && n == host_crash_at) _exit(77);}
// Durable installed record on the fixture SD, read back by recovery (installed_saved).
#define HOST_RECORDS DATA_DIR "/installed.json"
static bool host_records_save(json_t *all) {
 host_mutation();
 if (json_dump_file(all, HOST_RECORDS ".tmp", JSON_INDENT(1))) return false;
 host_mutation();
 return rename(HOST_RECORDS ".tmp", HOST_RECORDS) == 0;
}
static json_t *installed_saved(void) {json_t *j=json_load_file(HOST_RECORDS,0,NULL);return json_is_object(j)?j:(json_decref(j),json_object());}
static bool installed_commit_transaction(App*a,const char*v,bool e,bool c,json_t*f,json_t*d,json_t*h,const char*tx) {
 if (!host_commit_ok) return false;
 if (!a || !a->id) return true;
 json_t *all=installed_saved();
 json_object_set_new(all,a->id,json_pack("{s:s,s:s,s:O}","version",v,"transaction",tx?tx:"","files",f?f:json_array()));
 bool ok=host_records_save(all);json_decref(all);return ok;
}
static bool installed_commit_manifest(App*a,const char*v,bool e,bool c,json_t*f,json_t*d,json_t*h) {return installed_commit_transaction(a,v,e,c,f,d,h,NULL);}
static bool installed_commit(App*a,const char*v,bool e,bool c,json_t*f,json_t*d) {return installed_commit_manifest(a,v,e,c,f,d,NULL);}
static bool installed_forget(App*a) {
 if (!host_commit_ok) return false;
 if (!a || !a->id) return true;
 json_t *all=installed_saved();json_object_del(all,a->id);bool ok=host_records_save(all);json_decref(all);return ok;
}
#include "../source/detect_rules.h"
static int app_title_ids(App*a,u64*out,int max) {return a?detect_titles(a->j,out,max):0;}
static bool app_requirement_met(App*a,json_t*r) {return false;}
static json_t *app_install_steps(App*a,bool cia) {return json_object_get(json_object_get(a->j,"install"),cia?"cia":"3dsx");}
static bool app_has_format(App*a,bool cia) {return json_array_size(app_install_steps(a,cia))>0;}
static json_t *forge_mode_plan(const char *id,json_t *base) {return json_deep_copy(base);}
static bool app_installed_cia(App *a) {return false;}
static json_t *host_forge_data;  // personal files of the app under test
static json_t *forge_mode_data(const char *id) {return host_forge_data;}
static json_t *app_release(App*a,bool e) {return json_object_get(json_object_get(a->j,"releases"),e?"experimental":"stable");}
static bool app_release_experimental(App*a,json_t*r) {return r==json_object_get(json_object_get(a->j,"releases"),"experimental");}
static json_t *app_asset(App*a,json_t*r,const char*k) {return json_object_get(json_object_get(r,"assets"),k);}
static void LightLock_Lock(LightLock*l) {}
static void LightLock_Unlock(LightLock*l) {}
static void LightLock_Init(LightLock*l) {}
static void LightEvent_Init(LightEvent*l,int r) {}
static void LightEvent_Wait(LightEvent*l) {}
static void LightEvent_Signal(LightEvent*l) {}
static void svcGetThreadPriority(s32*p,int h) {*p=0x30;}
static void svcSleepThread(u64 ns);
static u64 svcGetSystemTick(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (u64)t.tv_sec*1000000000ULL+(u64)t.tv_nsec;}
static Thread threadCreate(void(*fn)(void*),void*a,size_t sz,int p,int c,bool d) {return (Thread)1;}
static void threadJoin(Thread t,u64 n) {}
static void threadFree(Thread t) {}
static Result amInit(void) {return 0;}
static void amExit(void) {}
static Result socInit(void*p,size_t n) {return 0;}
static void socExit(void) {}
static void reader_exit(void) {}
static bool dsp_dump(char*p,size_t n) {return false;}
typedef CC_SHA256_CTX mbedtls_sha256_context;
typedef CCCryptorRef mbedtls_aes_context;
#define MBEDTLS_AES_ENCRYPT 1
static void mbedtls_aes_init(mbedtls_aes_context *c) { *c = NULL; }
static int mbedtls_aes_setkey_enc(mbedtls_aes_context *c, const u8 *key, unsigned bits) {
    return CCCryptorCreate(kCCEncrypt,kCCAlgorithmAES,kCCOptionECBMode,key,bits/8,NULL,c);
}
static int mbedtls_aes_crypt_ecb(mbedtls_aes_context *c,int mode,const u8 in[16],u8 out[16]) {
    size_t n = 0;
    return CCCryptorUpdate(*c,in,16,out,16,&n) != kCCSuccess || n != 16;
}
static void mbedtls_aes_free(mbedtls_aes_context *c) { if (*c) CCCryptorRelease(*c); }
static void mbedtls_sha256_init(mbedtls_sha256_context*c) {CC_SHA256_Init(c);}
static int mbedtls_sha256_starts_ret(mbedtls_sha256_context*c,int n) {return !CC_SHA256_Init(c);}
static int mbedtls_sha256_update_ret(mbedtls_sha256_context*c,const void*p,size_t n) {return !CC_SHA256_Update(c,p,(CC_LONG)n);}
static int mbedtls_sha256_finish_ret(mbedtls_sha256_context*c,u8*p) {return !CC_SHA256_Final(p,c);}
static void mbedtls_sha256_free(mbedtls_sha256_context*c) {}
static void network_options(void*c) {}
static void network_error(char*out,size_t n,const char*s) {snprintf(out,n,"%s",s);}
typedef struct {u64 titleID; u64 size; uint16_t version;} AM_TitleInfo;
// Mocked AM. A fixture package "CIA:<title hex>:<version>" names its title and version;
// any other bytes are the original fixed test title, version 1. With host_am_file the
// installed title lives on the fixture SD, so a crashed child process leaves it behind.
static const char *host_fs_path;
static u64 host_title;
static uint16_t host_version = 1;
static bool host_am_fail, host_am_file, host_am_fail_finish;
static int host_am_starts, host_am_overwrites;
static char host_am_head[64];
static size_t host_am_head_len;
static void host_am_load(void) {
 if (!host_am_file) return;
 json_t *j=json_load_file("am-state.json",0,NULL);
 const char *t=json_string_value(json_object_get(j,"title"));
 host_title=t?strtoull(t,NULL,16):0;host_version=(uint16_t)json_integer_value(json_object_get(j,"version"));json_decref(j);
}
static void host_am_save(void) {
 if (!host_am_file) return;
 char t[17];snprintf(t,sizeof t,"%016llx",(unsigned long long)host_title);
 json_t *j=json_pack("{s:s,s:i}","title",t,"version",(int)host_version);json_dump_file(j,"am-state.json",0);json_decref(j);
}
static void host_cia_parse(const char *b,size_t n,u64 *title,uint16_t *version) {
 *title=0x0004000012345600ULL;*version=1;
 char head[64]={0};memcpy(head,b,n<sizeof head-1?n:sizeof head-1);
 unsigned long long t;unsigned v;
 if (!strncmp(head,"CIA:",4) && sscanf(head+4,"%16llx:%u",&t,&v)==2) {*title=t;*version=(uint16_t)v;}
}
static const char *fsMakePath(int kind,const char*p) {return p;}
static Result FSUSER_OpenFileDirectly(Handle*h,int arch,const char*a,const char*p,int flags,int attr) {host_fs_path=p; char full[600];snprintf(full,sizeof full,"sdmc:%s",p);FILE*f=fopen(full,"rb");if(!f)return -1;fclose(f);*h=1;return 0;}
static Result AM_GetCiaFileInfo(int media,AM_TitleInfo*i,Handle h) {
 char full[600],b[64]={0};snprintf(full,sizeof full,"sdmc:%s",host_fs_path);FILE*f=fopen(full,"rb");size_t n=f?fread(b,1,sizeof b,f):0;if(f)fclose(f);
 host_cia_parse(b,n,&i->titleID,&i->version);return 0;
}
static Result FSFILE_Close(Handle h) {return 0;}
static Result AM_GetTitleCount(int media,u32*n) {host_am_load();*n=host_title?1:0;return 0;}
static Result AM_GetTitleList(u32*n,int media,u32 cap,u64*t) {host_am_load();*n=host_title?1:0;if(*n)t[0]=host_title;return 0;}
static Result AM_GetTitleInfo(int m,u32 n,u64*t,AM_TitleInfo*i) {host_am_load();i->titleID=host_title;i->version=host_version;return host_title&&*t==host_title?0:-1;}
static Result AM_DeleteUserTitle(int m,u64 t) {host_mutation();host_am_load();if(t==host_title)host_title=0;host_am_save();return 0;}
static int host_tickets_deleted;
static Result AM_DeleteTitle(int m,u64 t) {host_mutation();host_am_load();if(t!=host_title)return -1;host_title=0;host_am_save();return 0;}
static Result AM_DeleteTicket(u64 t) {host_tickets_deleted++;return 0;}
static Result AM_StartCiaInstallOverwrite(Handle*h,int m) {host_am_overwrites++;host_am_starts++;host_am_head_len=0;*h=1;return host_am_fail?-1:0;}
static Result AM_StartCiaInstall(int m,Handle*h) {host_am_starts++;host_am_head_len=0;*h=1;return host_am_fail?-1:0;}
static void (*host_am_observe)(bool finishing);
static Result FSFILE_Write(Handle h,u32*w,u64 off,const void*b,size_t n,int flags) {
 if(host_am_observe)host_am_observe(false);
 if (off==0) {host_am_head_len=n<sizeof host_am_head?n:sizeof host_am_head;memcpy(host_am_head,b,host_am_head_len);}
 *w=(u32)n;return 0;
}
static Result AM_CancelCIAInstall(Handle h) {return 0;}
// AM commits atomically: the title changes only once the install finishes.
static Result AM_FinishCiaInstall(Handle h) {
 if(host_am_observe)host_am_observe(true);
 host_mutation();
 if (host_am_fail_finish) return -1;
 u64 t;uint16_t v;host_cia_parse(host_am_head,host_am_head_len,&t,&v);
 host_title=t;host_version=v;host_am_save();
 host_mutation();
 return 0;
}

#include <errno.h>
static Job *host_cancel_on_write;
static long host_write_budget = -1;
static int host_rename_fail_at, host_rename_calls;
static size_t host_fwrite(const void*p,size_t sz,size_t n,FILE*f) {
 host_mutation();
 if (host_cancel_on_write) {host_cancel_on_write->cancel=true;host_cancel_on_write=NULL;}
 if (host_write_budget < 0) return fwrite(p,sz,n,f);
 size_t allowed = sz ? (size_t)host_write_budget/sz : n;
 if (n>allowed) n=allowed;
 size_t wrote=fwrite(p,sz,n,f);host_write_budget-=(long)(wrote*sz);
 return wrote;
}
static bool host_fail_old_rename;  // a backup (NNNN.old) cannot be moved back
static int host_rename(const char*a,const char*b) {
 host_mutation();
 if (host_rename_fail_at && ++host_rename_calls == host_rename_fail_at) {errno=EIO;return -1;}
 size_t n=strlen(a);
 if (host_fail_old_rename && n>4 && !strcmp(a+n-4,".old")) {errno=EIO;return -1;}
 return rename(a,b);
}
static int host_remove(const char*p) {host_mutation();return remove(p);}
static int host_rmdir(const char*p) {host_mutation();return rmdir(p);}
static int host_mkdir(const char*p,mode_t m) {host_mutation();return mkdir(p,m);}
static FILE *host_fopen(const char*p,const char*m) {if(strpbrk(m,"wa+"))host_mutation();return fopen(p,m);}
#define fwrite host_fwrite
#define rename host_rename
#define remove host_remove
#define rmdir host_rmdir
#define mkdir host_mkdir
#define fopen host_fopen
