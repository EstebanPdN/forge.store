// Host test: the store finds ROMs under any name the ports accept (source/rom_find.c).
#include "../source/rom_find.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
char rom_find_root[64];
static void put(const char *rel, size_t size, size_t at, const char *bytes) {
    char p[256]; snprintf(p, sizeof p, "%s%s", rom_find_root, rel);
    unsigned char *b = calloc(1, size); assert(b);
    if (bytes) memcpy(b + at, bytes, strlen(bytes));
    FILE *f = fopen(p, "wb"); assert(f && fwrite(b, 1, size, f) == size); fclose(f); free(b);
}
static bool found(const char *rules, char *path, bool *derived) {
    json_error_t e; json_t *r = json_loads(rules, 0, &e); assert(r);
    bool ok = rom_find_scan(r, path, 300, derived); json_decref(r); return ok;
}
int main(void) {
    strcpy(rom_find_root, "/tmp/forge-romfind-XXXXXX"); assert(mkdtemp(rom_find_root));
    char d[128]; snprintf(d, sizeof d, "%s/3ds", rom_find_root); mkdir(d, 0755);
    snprintf(d, sizeof d, "%s/3ds/tmc", rom_find_root); mkdir(d, 0755);
    char path[300]; bool derived = true;
    const char *tmc = "{\"detect\":[{\"dir\":\"/3ds/tmc/\",\"ext\":[\"gba\"],\"minSize\":16777216,\"magic\":[{\"at\":172,\"any\":[\"BZME\",\"BZMP\"]}]}]}";
    assert(!found(tmc, path, &derived));
    put("/3ds/tmc/._europe.gba", 16777216, 172, "BZMP");     // macOS metadata: ignored
    put("/3ds/tmc/japan.gba", 16777216, 172, "BZMJ");         // region the port rejects
    put("/3ds/tmc/short.GBA", 1024, 172, "BZMP");             // too small
    assert(!found(tmc, path, &derived));
    put("/3ds/tmc/europe.GBA", 16777216, 172, "BZMP");
    assert(found(tmc, path, &derived) && strstr(path, "/3ds/tmc/europe.GBA") && !derived);
    // CRC of the last 1 MiB, with or without a 512-byte copier header; derived fallback.
    snprintf(d, sizeof d, "%s/3ds/sf", rom_find_root); mkdir(d, 0755);
    unsigned char *mb = calloc(1, 1048576); mb[5] = 7;
    char crc[9]; snprintf(crc, sizeof crc, "%08lx", crc32(crc32(0, NULL, 0), mb, 1048576)); free(mb);
    char sf[512]; snprintf(sf, sizeof sf, "{\"detect\":[{\"dir\":\"/3ds/sf\",\"ext\":[\"sfc\",\"smc\"],\"sizes\":[1048576,1049088],\"crc32\":{\"tail\":1048576,\"any\":[\"%s\"]}},"
             "{\"dir\":\"/3ds/sf/\",\"names\":[\"Starfox-Assets.BIN\"],\"magic\":[{\"at\":0,\"any\":[\"SFOXAS01\"]}],\"derived\":true}]}", crc);
    put("/3ds/sf/Starfox-Assets.bin", 64, 0, "SFOXAS01");
    assert(found(sf, path, &derived) && derived);
    put("/3ds/sf/my star fox.smc", 1049088, 512 + 5, "\x07");
    assert(found(sf, path, &derived) && strstr(path, "my star fox.smc") && !derived);
    put("/3ds/sf/my star fox.smc", 1049088, 512 + 5, "\x08");  // other contents: CRC differs
    derived = false; assert(found(sf, path, &derived) && derived);
    // Exact names and hex magic (N64 big-endian).
    snprintf(d, sizeof d, "%s/3ds/mk", rom_find_root); mkdir(d, 0755);
    const char *mk = "{\"detect\":[{\"dir\":\"/3ds/mk/\",\"names\":[\"Mario Kart 64.z64\",\"mk64.z64\"],\"sizes\":[4096],\"magic\":[{\"at\":0,\"hex\":[\"80371240\"]}]}]}";
    put("/3ds/mk/rom.z64", 4096, 0, "\x80\x37\x12\x40");
    assert(!found(mk, path, &derived));
    put("/3ds/mk/Mario Kart 64.z64", 4096, 0, "\x37\x80\x40\x12");  // byte-swapped .v64
    assert(!found(mk, path, &derived));
    put("/3ds/mk/mk64.z64", 4096, 0, "\x80\x37\x12\x40");
    assert(found(mk, path, &derived) && strstr(path, "mk64.z64"));
    assert(!found("{\"detect\":[{\"dir\":\"/3ds/../etc\",\"ext\":[\"gba\"]}]}", path, &derived));
    puts("PASS: any accepted name, ignored dot files, sizes, header bytes, CRC with copier header, derived data, exact names, unsafe folders");
}
