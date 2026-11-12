#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
typedef int32_t s32;
typedef uint64_t u64;
#define U64_MAX UINT64_MAX
#define DATA_DIR "sdmc:/3ds/forge-store"
#define CUR_THREAD_HANDLE 0
typedef struct HostThread {pthread_t id;void(*fn)(void*);void*arg;} *Thread;
static void *host_thread(void *p){Thread t=p;t->fn(t->arg);return NULL;}
static Thread threadCreate(void(*fn)(void*),void*arg,size_t stack,int priority,int core,bool detached){
    (void)stack;(void)priority;(void)core;(void)detached;
    Thread t=calloc(1,sizeof*t);if(!t)return NULL;t->fn=fn;t->arg=arg;
    if(pthread_create(&t->id,NULL,host_thread,t)){free(t);return NULL;}return t;
}
static void svcGetThreadPriority(s32*p,int ignored){(void)ignored;*p=0x30;}
static int threadJoin(Thread t,u64 timeout){(void)timeout;return pthread_join(t->id,NULL);}
static void threadFree(Thread t){free(t);}
static bool thread_reap(Thread*t){if(*t){threadJoin(*t,U64_MAX);threadFree(*t);*t=NULL;}return true;}
static bool thread_done(Thread*t){return thread_reap(t);}
// Host crypto adapter: native builds use mbedTLS. Tests verify the real envelope,
// authentication, limits, worker handoff and persistence against independent Node vectors.
typedef struct {unsigned char key[32];} mbedtls_gcm_context;
#define MBEDTLS_CIPHER_ID_AES 1
#define MBEDTLS_MD_SHA256 1
typedef int mbedtls_md_context_t;
static void mbedtls_gcm_init(mbedtls_gcm_context*c){memset(c,0,sizeof*c);}
static void mbedtls_gcm_free(mbedtls_gcm_context*c){memset(c,0,sizeof*c);}
static int mbedtls_gcm_setkey(mbedtls_gcm_context*c,int id,const unsigned char*k,unsigned bits){(void)id;if(bits!=256)return -1;memcpy(c->key,k,32);return 0;}
static int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context*c,size_t n,const unsigned char*iv,size_t ivn,const unsigned char*aad,size_t an,const unsigned char*tag,size_t tn,const unsigned char*input,unsigned char*out){
    EVP_CIPHER_CTX *x=EVP_CIPHER_CTX_new();int len=0,total=0,ok=x&&EVP_DecryptInit_ex(x,EVP_aes_256_gcm(),NULL,NULL,NULL)==1;
    if(ok)ok=EVP_CIPHER_CTX_ctrl(x,EVP_CTRL_GCM_SET_IVLEN,(int)ivn,NULL)==1&&EVP_DecryptInit_ex(x,NULL,NULL,c->key,iv)==1;
    if(ok)ok=EVP_DecryptUpdate(x,NULL,&len,aad,(int)an)==1&&EVP_DecryptUpdate(x,out,&len,input,(int)n)==1;
    total=len;if(ok)ok=EVP_CIPHER_CTX_ctrl(x,EVP_CTRL_GCM_SET_TAG,(int)tn,(void*)tag)==1&&EVP_DecryptFinal_ex(x,out+total,&len)==1;
    EVP_CIPHER_CTX_free(x);return ok?0:-1;
}
typedef SHA256_CTX mbedtls_sha256_context;
static void mbedtls_sha256_init(mbedtls_sha256_context*c){memset(c,0,sizeof*c);}
static void mbedtls_sha256_free(mbedtls_sha256_context*c){memset(c,0,sizeof*c);}
static void mbedtls_sha256_clone(mbedtls_sha256_context*d,const mbedtls_sha256_context*s){*d=*s;}
static int mbedtls_sha256_starts_ret(mbedtls_sha256_context*c,int s){(void)s;return SHA256_Init(c)?0:-1;}
static int mbedtls_sha256_update_ret(mbedtls_sha256_context*c,const unsigned char*p,size_t n){return SHA256_Update(c,p,n)?0:-1;}
static int mbedtls_sha256_finish_ret(mbedtls_sha256_context*c,unsigned char*p){return SHA256_Final(p,c)?0:-1;}
static int mbedtls_sha256_ret(const unsigned char*p,size_t n,unsigned char*out,int sha224){(void)sha224;return SHA256(p,n,out)?0:-1;}
static u64 osGetTime(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000ull+t.tv_nsec/1000000;}
#define log_line(...) ((void)0)
