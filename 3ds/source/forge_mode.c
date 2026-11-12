// Encrypted personal-resource manifest. No password, plaintext URL or ROM is
// built into the executable. Derivation/decryption and SD persistence run on a
// worker; installation still uses the normal verified, transactional downloader.
#ifdef FORGE_MODE_HOST_TEST
#include "../tests/forge-mode-host.h"
#else
#include "common.h"
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#endif
#include "forge_mode.h"
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#define SEALED_PATH "romfs:/personal-assets.fme"
#define KEY_PATH DATA_DIR "/forge-mode.key"
#define MANIFEST_MAX 16384u
static const unsigned char aad[] = "forge.store/private-assets/v1";
static Thread worker;
static bool done, restore;
static u64 derive_ms;
static char password[65];
static ForgeModeState state = FORGE_LOCKED, result_state;
static json_t *manifest, *result_manifest;
static void wipe(void *data,size_t n) {volatile unsigned char *p=data;while(n--)*p++=0;}
#include "forge_kdf.h"
static unsigned get32(const unsigned char *p) {return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}

static bool valid_manifest(json_t *root) {
    if(!json_is_object(root)||json_object_size(root)>32)return false;
    const char *id;json_t *steps;
    json_object_foreach(root,id,steps) {
        if(!*id||strlen(id)>64||!json_is_array(steps)||json_array_size(steps)>4)return false;
        for(const char *p=id;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='-'&&*p!='_')return false;
        size_t i;json_t *s;
        json_array_foreach(steps,i,s) {
            const char *type=json_string_value(json_object_get(s,"type")),*url=json_string_value(json_object_get(s,"url"));
            const char *out=json_string_value(json_object_get(s,"output")),*hash=json_string_value(json_object_get(s,"sha256"));
            const char *name=json_string_value(json_object_get(s,"name"));
            json_int_t size=json_integer_value(json_object_get(s,"size"));
            if(!type||strcmp(type,"downloadUrl")||!json_is_true(json_object_get(s,"personal"))||!url||strncmp(url,"https://",8)||strlen(url)>1024||
               !out||strncmp(out,"/3ds/",5)||strlen(out)>240||strstr(out,"..")||!hash||strlen(hash)!=64||!name||!*name||strlen(name)>96||size<=0||size>128*1024*1024)return false;
            for(int k=0;k<64;k++)if(!isxdigit((unsigned char)hash[k]))return false;
        }
    }
    return true;
}

static json_t *decrypt(const unsigned char *blob,size_t length,const unsigned char key[32]) {
    if(length<56||memcmp(blob,"FM01",4)||get32(blob+52)!=length-56||length>MANIFEST_MAX+56)return NULL;
    size_t n=length-56;unsigned char *plain=calloc(1,n+1);if(!plain)return NULL;
    mbedtls_gcm_context gcm;mbedtls_gcm_init(&gcm);
    int err=mbedtls_gcm_setkey(&gcm,MBEDTLS_CIPHER_ID_AES,key,256);
    if(!err)err=mbedtls_gcm_auth_decrypt(&gcm,n,blob+24,12,aad,sizeof aad-1,blob+36,16,blob+56,plain);
    mbedtls_gcm_free(&gcm);
    json_t *j=err?NULL:json_loadb((char*)plain,n,JSON_REJECT_DUPLICATES,NULL);
    wipe(plain,n);free(plain);
    if(!valid_manifest(j)){json_decref(j);j=NULL;}
    return j;
}

static bool persist_key(const unsigned char hash[32],const unsigned char key[32]) {
    unsigned char saved[68];memcpy(saved,"FMK1",4);memcpy(saved+4,hash,32);memcpy(saved+36,key,32);
    FILE *f=fopen(KEY_PATH ".tmp","wb");bool ok=false;
    if(f){ok=fwrite(saved,1,sizeof saved,f)==sizeof saved;if(fflush(f)||fsync(fileno(f)))ok=false;if(fclose(f))ok=false;}
    wipe(saved,sizeof saved);
    if(!ok||rename(KEY_PATH ".tmp",KEY_PATH)){remove(KEY_PATH ".tmp");return false;}
    return true;
}
static void unlock_worker(void *unused) {
    (void)unused;
    unsigned char key[32]={0},digest[32],*blob=NULL;size_t length=0;
    result_state=restore?FORGE_LOCKED:FORGE_INVALID;result_manifest=NULL;
    FILE *f=fopen(SEALED_PATH,"rb");
    if(!f)goto end;
    if(fseek(f,0,SEEK_END)){fclose(f);goto end;}
    long len=ftell(f);rewind(f);
    if(len<56||len>MANIFEST_MAX+56){fclose(f);goto end;}
    length=(size_t)len;blob=malloc(length);
    bool read_ok=blob&&fread(blob,1,length,f)==length&&!ferror(f);fclose(f);
    if(!read_ok||memcmp(blob,"FM01",4))goto end;
    unsigned iterations=get32(blob+4);if(iterations<10000||iterations>200000)goto end;
    mbedtls_sha256_ret(blob,length,digest,0);
    if(restore) {
        unsigned char saved[69];f=fopen(KEY_PATH,"rb");if(!f)goto end;
        size_t n=fread(saved,1,sizeof saved,f);fclose(f);
        bool valid=n==68&&!memcmp(saved,"FMK1",4)&&!memcmp(saved+4,digest,32);
        if(valid)memcpy(key,saved+36,32);
        wipe(saved,sizeof saved);if(!valid)goto end;
    } else {
        u64 started=osGetTime();
        int err=forge_derive_key((unsigned char*)password,strlen(password),blob+8,iterations,key);
        derive_ms=osGetTime()-started;
        wipe(password,sizeof password);if(err)goto end;
    }
    result_manifest=decrypt(blob,length,key);
    if(result_manifest) {
        if(restore||persist_key(digest,key))result_state=FORGE_ENABLED;
        else {json_decref(result_manifest);result_manifest=NULL;result_state=FORGE_STORAGE_ERROR;}
    }
end:
    wipe(password,sizeof password);wipe(key,sizeof key);free(blob);
    __atomic_store_n(&done,true,__ATOMIC_RELEASE);
}
static bool begin(bool from_disk) {
    if(worker)return false;
    restore=from_disk;done=false;derive_ms=0;state=FORGE_CHECKING;
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    worker=threadCreate(unlock_worker,NULL,48*1024,priority+1,-2,false);
    if(!worker){wipe(password,sizeof password);state=FORGE_STORAGE_ERROR;return false;}
    return true;
}
void forge_mode_init(void) {begin(true);}
void forge_mode_pump(void) {
    if(!worker||!__atomic_load_n(&done,__ATOMIC_ACQUIRE))return;
    if(!thread_done(&worker))return;
    json_decref(manifest);manifest=result_manifest;result_manifest=NULL;state=result_state;
}
bool forge_mode_unlock(const char *code) {
    if(worker||state==FORGE_ENABLED||!code||!*code||strlen(code)>=sizeof password)return false;
    snprintf(password,sizeof password,"%s",code);return begin(false);
}
ForgeModeState forge_mode_state(void){return state;}
unsigned forge_mode_derivation_ms(void){return state==FORGE_CHECKING?0:(unsigned)derive_ms;}
bool forge_mode_enabled(void){return state==FORGE_ENABLED&&manifest;}
json_t *forge_mode_plan(const char *id,json_t *base) {
    if(!json_is_array(base)||!json_array_size(base))return NULL;
    json_t *plan=json_array();if(!plan)return NULL;
    json_t *extra=forge_mode_enabled()?json_object_get(manifest,id):NULL;
    if((extra&&json_array_extend(plan,extra))||json_array_extend(plan,base)){json_decref(plan);return NULL;}
    json_t *copy=json_deep_copy(plan);json_decref(plan);return copy;
}
void forge_mode_exit(void) {
    if(!thread_reap(&worker))return;
    json_decref(manifest);json_decref(result_manifest);manifest=result_manifest=NULL;wipe(password,sizeof password);
}
