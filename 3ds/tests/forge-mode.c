#define FORGE_MODE_HOST_TEST
#include "../source/forge_mode.c"
#include <assert.h>
static void settle(void){while(!__atomic_load_n(&done,__ATOMIC_ACQUIRE))usleep(1000);forge_mode_pump();}
int main(void){
    // Compare the optimized production KDF with OpenSSL, including full work
    // factor, UTF-8, all-zero salt and the maximum accepted password length.
    const char *codes[]={"a", "fixture-ñ-3ds", "0123456789012345678901234567890123456789012345678901234567890123"};
    unsigned rounds[]={1,10000,100000};
    for(unsigned k=0;k<3;k++){
        unsigned char salt[16]={0},a[32],b[32];salt[0]=(unsigned char)k;
        assert(!forge_derive_key((const unsigned char*)codes[k],strlen(codes[k]),salt,rounds[k],a));
        assert(PKCS5_PBKDF2_HMAC(codes[k],(int)strlen(codes[k]),salt,16,rounds[k],EVP_sha256(),32,b));
        assert(!memcmp(a,b,32));
    }
    forge_mode_init();settle();assert(forge_mode_state()==FORGE_LOCKED);
    assert(!forge_mode_unlock(""));assert(forge_mode_unlock("wrong-fixture-code"));assert(!forge_mode_unlock("again"));settle();assert(forge_mode_state()==FORGE_INVALID);
    assert(forge_mode_unlock("host-fixture-only"));settle();assert(forge_mode_enabled());assert(!password[0]);
    json_t *base=json_loads("[{\"type\":\"requireFile\",\"ref\":\"rom\"}]",0,NULL);
    json_t *plan=forge_mode_plan("demo",base);assert(json_array_size(plan)==2);assert(json_array_size(base)==1);
    assert(json_is_true(json_object_get(json_array_get(plan,0),"personal")));json_decref(plan);
    plan=forge_mode_plan("unknown",base);assert(json_array_size(plan)==1);json_decref(plan);json_decref(base);
    forge_mode_exit();state=FORGE_LOCKED;forge_mode_init();settle();assert(forge_mode_enabled());
    FILE*f=fopen(SEALED_PATH,"rb");assert(f);unsigned char blob[4096];size_t n=fread(blob,1,sizeof blob,f);fclose(f);
    f=fopen(KEY_PATH,"rb");assert(f);unsigned char keyfile[68];assert(fread(keyfile,1,68,f)==68);fclose(f);
    json_t*j=decrypt(blob,n,keyfile+36);assert(j);json_decref(j);
    blob[36]^=1;assert(!decrypt(blob,n,keyfile+36));blob[36]^=1;
    assert(!decrypt(blob,n-1,keyfile+36));assert(!decrypt(blob,20,keyfile+36));
    assert(!valid_manifest(json_null()));
    j=json_loads("{\"demo\":[{\"type\":\"downloadUrl\",\"personal\":true,\"url\":\"http://example.invalid\"}]}",0,NULL);assert(!valid_manifest(j));json_decref(j);
    forge_mode_exit();
    f=fopen(KEY_PATH,"wb");assert(f);assert(fwrite("bad",1,3,f)==3);fclose(f);state=FORGE_LOCKED;forge_mode_init();settle();assert(!forge_mode_enabled());forge_mode_exit();
    puts("PASS: authenticated envelope, wrong code, tampering, bounds, persistent unlock, plan ownership and worker handoff");
}
