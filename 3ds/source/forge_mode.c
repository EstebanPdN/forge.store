#include "install_rules.h"
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
#include "forge_zip.h"
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#define SEALED_PATH "romfs:/personal-assets.fme"
#define KEY_PATH DATA_DIR "/forge-mode.key"
#define FACTORY_KEY_PATH "romfs:/forge-default.key"
#define MANIFEST_MAX (192u*1024u)
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

static bool valid_download(json_t *step, const char *package) {
    const char *type=json_string_value(json_object_get(step,"type"));
    const char *url=json_string_value(json_object_get(step,"url"));
    const char *out=json_string_value(json_object_get(step,"output"));
    const char *name=json_string_value(json_object_get(step,"name"));
    const char *hash=json_string_value(json_object_get(step,"sha256"));
    json_int_t size=json_integer_value(json_object_get(step,"size"));
    json_t *member=json_object_get(step,"zipMember");
    if (!type || strcmp(type,"downloadUrl") || !url || strncmp(url,"https://",8) || strlen(url)>1024 ||
        !name || !*name || strlen(name)>96 || !out || strlen(out)>240 || strstr(out,"..") ||
        !hash || strlen(hash)!=64 || size<=0 || size>(member?INT32_MAX:128*1024*1024)) return false;
    if (package) {
        if (strcmp(out,package) || !member || json_object_get(step,"personal")) return false;
    } else if (!json_is_true(json_object_get(step,"personal")) || (strncmp(out,"/3ds/",5) && strncmp(out,"/vb/",4))) return false;
    if (json_object_get(step,"unpackTo")) {
        const char *dir=json_string_value(json_object_get(step,"unpackTo"));
        if (package || !member || strncmp(out,"/3ds/forge-store/tmp/",21) ||
            !dir || strncmp(dir,"/3ds/",5) || strlen(dir)<=5 || strlen(dir)>240 ||
            dir[strlen(dir)-1]!='/' || strstr(dir,"..")) return false;
    }
    for (unsigned i=0;i<64;i++) if (forge_hex_digit(hash[i])<0) return false;
    return !member || forge_zip_member(member,NULL);
}

// Derive the only allowed executable operations from an authenticated CIA descriptor.
static bool prepare_build(const char *id,json_t *build) {
    const char *tag=json_string_value(json_object_get(build,"tag"));
    const char *title=json_string_value(json_object_get(build,"forgeTitleId"));
    if (!tag || !*tag || strlen(tag)>40 || !title || strlen(title)!=16 || strncmp(title,"00040000",8) ||
        !json_is_boolean(json_object_get(build,"o3ds"))) return false;
    for (unsigned i=8;i<16;i++) if (forge_hex_digit(title[i])<0) return false;
    char path[128];
    snprintf(path,sizeof path,"/3ds/forge-store/tmp/forge-%s.cia",id);
    json_t *download=json_object_get(build,"download");
    if (!valid_download(download,path)) return false;
    json_t *steps=json_pack("[O,{s:s,s:s},{s:s,s:s}]",download,"type","installCia","file",path,
                            "type","deleteFile","file",path);
    return steps && json_object_set_new(build,"steps",steps)==0;
}

static bool valid_manifest(json_t *root) {
    if (!json_is_object(root) || json_object_size(root)>32) return false;
    const char *id; json_t *entry;
    json_object_foreach(root,id,entry) {
        if (!*id || strlen(id)>64) return false;
        for (const char *p=id;*p;p++) if (!isalnum((unsigned char)*p) && *p!='-' && *p!='_') return false;
        json_t *steps=entry;
        if (json_is_object(entry)) {
            if (!prepare_build(id,json_object_get(entry,"build"))) return false;
            steps=json_object_get(entry,"data");
        }
        if (!json_is_array(steps) || json_array_size(steps)>56) return false;
        size_t i; json_t *step;
        json_array_foreach(steps,i,step) if (!valid_download(step,NULL)) return false;
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
    // FAT cannot rename over an existing file. Keep the previous key usable
    // if the console loses power between the two directory operations.
    bool previous = access(KEY_PATH,F_OK)==0;
    if (ok && previous) {
        if (access(KEY_PATH ".bak",F_OK)==0 && remove(KEY_PATH ".bak")) ok=false;
        if (ok && rename(KEY_PATH,KEY_PATH ".bak")) ok=false;
    }
    if(!ok||rename(KEY_PATH ".tmp",KEY_PATH)){remove(KEY_PATH ".tmp");return false;}
    remove(KEY_PATH ".bak");
    return true;
}
static json_t *restore_manifest(const char *path, const unsigned char digest[32],
                                const unsigned char *blob, size_t length) {
    FILE *file=fopen(path,"rb");
    if (!file) return NULL;
    unsigned char saved[69];
    size_t n=fread(saved,1,sizeof saved,file);
    bool valid=n==68 && !ferror(file) && !memcmp(saved,"FMK1",4) && !memcmp(saved+4,digest,32);
    fclose(file);
    json_t *restored=valid ? decrypt(blob,length,saved+36) : NULL;
    wipe(saved,sizeof saved);
    return restored;
}

// FBI records a scanned URL before installing its CIA. The private QR supplies
// a random key in the URL fragment, separate from the downloadable package.
static bool read_install_qr(unsigned char key[32]) {
    FILE *file = fopen("sdmc:/fbi/lasturls", "rb");
    if (!file) return false;
    char url[2048];
    size_t size = fread(url, 1, sizeof url, file);
    bool ok = !ferror(file) && size < sizeof url && !memchr(url, 0, size);
    fclose(file);
    if (ok) {
        url[size] = 0;
        char *fragment = strchr(url, '#');
        const char prefix[] = "#forge-v1=";
        ok = !strncmp(url, "https://", 8) && !strpbrk(url, "\r\n") && fragment &&
             !strncmp(fragment, prefix, sizeof prefix - 1) && strlen(fragment + sizeof prefix - 1) == 64;
        if (ok) {
            const char *hex = fragment + sizeof prefix - 1;
            for (size_t i = 0; i < 32; ++i) {
                int hi = forge_hex_digit(hex[2*i]), lo = forge_hex_digit(hex[2*i+1]);
                if (hi < 0 || lo < 0) { ok = false; break; }
                key[i] = (unsigned char)((hi << 4) | lo);
            }
        }
    }
    wipe(url, sizeof url);
    return ok;
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
    bool save_key = !restore;
    if(restore) {
        result_manifest=restore_manifest(FACTORY_KEY_PATH,digest,blob,length);
        if (!result_manifest) result_manifest=restore_manifest(KEY_PATH,digest,blob,length);
        if (!result_manifest) result_manifest=restore_manifest(KEY_PATH ".bak",digest,blob,length);
        if (!result_manifest && read_install_qr(key)) {
            result_manifest=decrypt(blob,length,key);
            save_key=true;
        }
    } else {
        u64 started=osGetTime();
        int err=forge_derive_key((unsigned char*)password,strlen(password),blob+8,iterations,key);
        derive_ms=osGetTime()-started;
        wipe(password,sizeof password);if(err)goto end;
        result_manifest=decrypt(blob,length,key);
    }
    if(result_manifest) {
        if(!save_key||persist_key(digest,key))result_state=FORGE_ENABLED;
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
json_t *forge_mode_build(const char *id) {
    return id && forge_mode_enabled() ? json_object_get(json_object_get(manifest,id),"build") : NULL;
}
json_t *forge_mode_data(const char *id) {
    json_t *entry=id && forge_mode_enabled()?json_object_get(manifest,id):NULL;
    return json_is_array(entry)?entry:json_object_get(entry,"data");
}
json_t *forge_mode_plan(const char *id,json_t *base) {
    if(!json_is_array(base)||!json_array_size(base))return NULL;
    // Only the authenticated manifest may supply an encrypted member and its key.
    size_t i;json_t *step;
    if (base!=json_object_get(forge_mode_build(id),"steps"))
        json_array_foreach(base,i,step) if(json_object_get(step,"zipMember")||json_object_get(step,"unpackTo")||json_object_get(step,"personal"))return NULL;
    json_t *plan=json_array();if(!plan)return NULL;
    json_t *extra=forge_mode_data(id);
    bool cia=false;
    json_array_foreach(base,i,step) {
        const char *type=json_string_value(json_object_get(step,"type"));
        if(type && !strcmp(type,"installCia"))cia=true;
    }
    // Order: the game's folders, then every data file, then the app itself (the CIA last).
    json_array_foreach(base,i,step) {
        const char *type=json_string_value(json_object_get(step,"type"));
        if(type && !strcmp(type,"mkdir") && json_array_append(plan,step)){json_decref(plan);return NULL;}
    }
    json_array_foreach(extra,i,step)
        if(install_format_matches(step,cia) && json_array_append(plan,step)){json_decref(plan);return NULL;}
    json_array_foreach(base,i,step) {
        const char *type=json_string_value(json_object_get(step,"type"));
        if((!type || strcmp(type,"mkdir")) && json_array_append(plan,step)){json_decref(plan);return NULL;}
    }
    json_t *copy=json_deep_copy(plan);json_decref(plan);return copy;
}
void forge_mode_exit(void) {
    if(!thread_reap(&worker))return;
    json_decref(manifest);json_decref(result_manifest);manifest=result_manifest=NULL;wipe(password,sizeof password);
}
