#define FORGE_MODE_HOST_TEST
#include "../source/forge_mode.c"
#include <assert.h>
static void settle(void){while(!__atomic_load_n(&done,__ATOMIC_ACQUIRE))usleep(1000);forge_mode_pump();}
static void check_preview_plan(void) {
    json_t *build=forge_mode_build("preview"), *steps=json_object_get(build,"steps");
    assert(json_is_false(json_object_get(build,"o3ds")));
    json_t *plan=forge_mode_plan("preview",steps);
    const char *types[]={"downloadUrl","downloadUrl","installCia","deleteFile"};
    assert(json_array_size(plan)==4);
    for (size_t i=0;i<4;i++)
        assert(!strcmp(json_string_value(json_object_get(json_array_get(plan,i),"type")),types[i]));
    json_t *data=json_array_get(plan,0);
    assert(forge_wire_size(data)==170000000 && valid_download(data,NULL));
    json_object_del(data,"zipMember");
    assert(!valid_download(data,NULL));
    assert(json_object_get(json_array_get(forge_mode_data("preview"),0),"zipMember"));
    json_t *untrusted=json_deep_copy(steps);
    assert(!forge_mode_plan("preview",untrusted));
    json_t *download=json_array_get(untrusted,0);
    json_object_set_new(download,"output",json_string("/3ds/other.cia"));
    assert(!valid_download(download,"/3ds/forge-store/tmp/forge-preview.cia"));
    json_decref(untrusted);json_decref(plan);
}
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
    assert(!forge_mode_build("preview") && !forge_mode_data("preview"));
    assert(!forge_mode_unlock(""));assert(forge_mode_unlock("wrong-fixture-code"));assert(!forge_mode_unlock("again"));settle();assert(forge_mode_state()==FORGE_INVALID);
    assert(forge_mode_unlock("host-fixture-only"));settle();assert(forge_mode_enabled());assert(!password[0]);
    check_preview_plan();
    json_t *base=json_loads("[{\"type\":\"requireFile\",\"ref\":\"rom\"}]",0,NULL);
    json_t *plan=forge_mode_plan("demo",base);assert(json_array_size(plan)==2);assert(json_array_size(base)==1);
    assert(json_is_true(json_object_get(json_array_get(plan,0),"personal")));json_decref(plan);
    plan=forge_mode_plan("unknown",base);assert(json_array_size(plan)==1);json_decref(plan);json_decref(base);
    forge_mode_exit();state=FORGE_LOCKED;forge_mode_init();settle();assert(forge_mode_enabled());
    check_preview_plan();
    FILE*f=fopen(SEALED_PATH,"rb");assert(f);unsigned char blob[4096];size_t n=fread(blob,1,sizeof blob,f);fclose(f);
    f=fopen(KEY_PATH,"rb");assert(f);unsigned char keyfile[68];assert(fread(keyfile,1,68,f)==68);fclose(f);
    json_t*j=decrypt(blob,n,keyfile+36);assert(j);json_decref(j);
    blob[36]^=1;assert(!decrypt(blob,n,keyfile+36));blob[36]^=1;
    assert(!decrypt(blob,n-1,keyfile+36));assert(!decrypt(blob,20,keyfile+36));
    assert(!valid_manifest(json_null()));
    j=json_loads("{\"demo\":[{\"type\":\"downloadUrl\",\"personal\":true,\"url\":\"http://example.invalid\"}]}",0,NULL);assert(!valid_manifest(j));json_decref(j);
    forge_mode_exit();
    f=fopen(KEY_PATH,"wb");assert(f);assert(fwrite("bad",1,3,f)==3);fclose(f);state=FORGE_LOCKED;forge_mode_init();settle();assert(!forge_mode_enabled());forge_mode_exit();
    assert(!unlink(KEY_PATH));
    f=fopen(FACTORY_KEY_PATH,"wb");assert(f);
    assert(fwrite(keyfile,1,sizeof keyfile,f)==sizeof keyfile);assert(!fclose(f));
    forge_mode_init();settle();assert(forge_mode_enabled());check_preview_plan();forge_mode_exit();
    assert(access(KEY_PATH,F_OK)!=0);
    f=fopen(KEY_PATH,"wb");assert(f);assert(fwrite("old",1,3,f)==3);assert(!fclose(f));
    forge_mode_init();settle();assert(forge_mode_enabled());forge_mode_exit();
    keyfile[36]^=1;
    f=fopen(FACTORY_KEY_PATH,"wb");assert(f);
    assert(fwrite(keyfile,1,sizeof keyfile,f)==sizeof keyfile);assert(!fclose(f));
    forge_mode_init();settle();assert(!forge_mode_enabled());assert(!forge_mode_build("preview"));forge_mode_exit();
    puts("PASS: authenticated activation, default activation on fresh and existing SD, invalid keys, plan ownership and worker handoff");
}
