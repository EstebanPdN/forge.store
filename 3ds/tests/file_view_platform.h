// Host shim: execute the production worker with real files and pthreads.
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#define U64_MAX UINT64_MAX
#define CUR_THREAD_HANDLE 0
#define R_FAILED(r) ((r)!=0)
typedef int32_t s32;
static pthread_t test_main;
static unsigned test_stat_calls,test_workers,test_alloc_calls;
static bool test_hold,test_entered,test_fail_alloc,test_fail_thread;
static int file_view_stat(const char *path,struct stat *st) {
    assert(!pthread_equal(pthread_self(),test_main));
    __atomic_add_fetch(&test_stat_calls,1,__ATOMIC_RELAXED);
    __atomic_store_n(&test_entered,true,__ATOMIC_RELEASE);
    while(__atomic_load_n(&test_hold,__ATOMIC_ACQUIRE))usleep(1000);
    return stat(path,st);
}
static void *file_view_calloc(size_t n,size_t size) {
    test_alloc_calls++;
    return test_fail_alloc?NULL:calloc(n,size);
}
typedef struct HostThread {pthread_t id;void (*fn)(void *);void *arg;bool done;} *Thread;
static void *host_run(void *arg) {
    Thread t=arg;t->fn(t->arg);__atomic_store_n(&t->done,true,__ATOMIC_RELEASE);return NULL;
}
static Thread threadCreate(void (*fn)(void *),void *arg,size_t stack,int priority,int core,bool detached) {
    (void)stack;(void)core;(void)detached;assert(priority>=0&&priority<=0x3f);
    test_workers++;if(test_fail_thread)return NULL;
    Thread t=calloc(1,sizeof *t);assert(t);t->fn=fn;t->arg=arg;
    assert(!pthread_create(&t->id,NULL,host_run,t));return t;
}
static int threadJoin(Thread t,uint64_t timeout) {
    if(!timeout&&!__atomic_load_n(&t->done,__ATOMIC_ACQUIRE))return 1;
    return pthread_join(t->id,NULL);
}
static void threadFree(Thread t){free(t);}
static void svcGetThreadPriority(s32 *out,int handle){(void)handle;*out=0x30;}
#define stat(...) file_view_stat(__VA_ARGS__)
#define calloc file_view_calloc
