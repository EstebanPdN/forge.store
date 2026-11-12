#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <jansson.h>
#include <CommonCrypto/CommonDigest.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int Result;
typedef int Handle;
typedef void *Thread;
typedef int LightLock;
typedef int LightEvent;
typedef struct { json_t *j; const char *id, *repo; bool old_ok; } App;
#define DATA_DIR "sdmc:/3ds/forge-store"
#define DANGER 1
#define AMBER 2
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
static bool installed_commit(App*a,const char*v,bool e,bool c,json_t*f,json_t*d) {return host_commit_ok;}
static bool installed_commit_manifest(App*a,const char*v,bool e,bool c,json_t*f,json_t*d,json_t*h) {return host_commit_ok;}
static bool installed_forget(App*a) {return host_commit_ok;}
static bool app_requirement_met(App*a,json_t*r) {return false;}
static json_t *app_install_steps(App*a,bool cia) {return json_object_get(json_object_get(a->j,"install"),cia?"cia":"3dsx");}
static bool app_has_format(App*a,bool cia) {return json_array_size(app_install_steps(a,cia))>0;}
static json_t *forge_mode_plan(const char *id,json_t *base) {return json_deep_copy(base);}
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
static u64 svcGetSystemTick(void) {return 0;}
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
static void mbedtls_sha256_init(mbedtls_sha256_context*c) {CC_SHA256_Init(c);}
static int mbedtls_sha256_starts_ret(mbedtls_sha256_context*c,int n) {return !CC_SHA256_Init(c);}
static int mbedtls_sha256_update_ret(mbedtls_sha256_context*c,const void*p,size_t n) {return !CC_SHA256_Update(c,p,(CC_LONG)n);}
static int mbedtls_sha256_finish_ret(mbedtls_sha256_context*c,u8*p) {return !CC_SHA256_Final(p,c);}
static void mbedtls_sha256_free(mbedtls_sha256_context*c) {}
static void network_options(void*c) {}
static void network_error(char*out,size_t n,const char*s) {snprintf(out,n,"%s",s);}
typedef struct {u64 titleID; u64 size; uint16_t version;} AM_TitleInfo;
static const char *host_fs_path;
static u64 host_title;
static bool host_am_fail;
static int host_am_starts;
static const char *fsMakePath(int kind,const char*p) {return p;}
static Result FSUSER_OpenFileDirectly(Handle*h,int arch,const char*a,const char*p,int flags,int attr) {host_fs_path=p; char full[600];snprintf(full,sizeof full,"sdmc:%s",p);FILE*f=fopen(full,"rb");if(!f)return -1;fclose(f);*h=1;return 0;}
static Result AM_GetCiaFileInfo(int media,AM_TitleInfo*i,Handle h) {i->titleID=0x0004000012345600ULL;i->version=1;return 0;}
static Result FSFILE_Close(Handle h) {return 0;}
static Result AM_GetTitleCount(int media,u32*n) {*n=host_title?1:0;return 0;}
static Result AM_GetTitleList(u32*n,int media,u32 cap,u64*t) {*n=host_title?1:0;if(*n)t[0]=host_title;return 0;}
static Result AM_GetTitleInfo(int m,u32 n,u64*t,AM_TitleInfo*i) {i->titleID=host_title;i->version=1;return host_title?0:-1;}
static Result AM_DeleteUserTitle(int m,u64 t) {host_title=0;return 0;}
static Result AM_StartCiaInstallOverwrite(Handle*h,int m) {host_am_starts++;*h=1;return host_am_fail?-1:0;}
static Result AM_StartCiaInstall(int m,Handle*h) {host_am_starts++;*h=1;return host_am_fail?-1:0;}
static Result FSFILE_Write(Handle h,u32*w,u64 off,const void*b,size_t n,int flags) {*w=(u32)n;return 0;}
static Result AM_CancelCIAInstall(Handle h) {return 0;}
static Result AM_FinishCiaInstall(Handle h) {host_title=0x0004000012345600ULL;return 0;}

#include <errno.h>
static Job *host_cancel_on_write;
static long host_write_budget = -1;
static int host_rename_fail_at, host_rename_calls;
static size_t host_fwrite(const void*p,size_t sz,size_t n,FILE*f) {
 if (host_cancel_on_write) {host_cancel_on_write->cancel=true;host_cancel_on_write=NULL;}
 if (host_write_budget < 0) return fwrite(p,sz,n,f);
 size_t allowed = sz ? (size_t)host_write_budget/sz : n;
 if (n>allowed) n=allowed;
 size_t wrote=fwrite(p,sz,n,f);host_write_budget-=(long)(wrote*sz);
 return wrote;
}
static int host_rename(const char*a,const char*b) {
 if (host_rename_fail_at && ++host_rename_calls == host_rename_fail_at) {errno=EIO;return -1;}
 return rename(a,b);
}
#define fwrite host_fwrite
#define rename host_rename
