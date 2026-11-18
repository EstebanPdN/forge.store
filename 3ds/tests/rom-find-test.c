// Host test: the store finds ROMs under any name the ports accept (source/rom_find.c).
#include "../source/rom_find.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
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
// ---- the shipped catalog: every port's own rules ---------------------------------------------
static char *at_root(const char *rel) { static char p[300]; snprintf(p, sizeof p, "%s%s", rom_find_root, rel); return p; }
static void dirs(const char *rel) {  // mkdir -p below the root
    char p[300]; snprintf(p, sizeof p, "%s%s", rom_find_root, rel);
    for (char *c = p + strlen(rom_find_root) + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(p, 0755); *c = '/'; }
    mkdir(p, 0755);
}
static bool exists(const char *rel) { struct stat st; return !stat(at_root(rel), &st); }
static void gone(const char *rel) { assert(!unlink(at_root(rel))); }
/** An N64 ROM in .z64 order: the header word, the fields at their offsets, a body that shows any byte swap. */
static unsigned char *n64(size_t size, unsigned at1, const char *f1, unsigned at2, const char *f2, size_t n2) {
    unsigned char *b = malloc(size); assert(b);
    for (size_t i = 0; i < size; i++) b[i] = (unsigned char)(i * 7 + (i >> 9));
    memcpy(b, "\x80\x37\x12\x40", 4); memset(b + 0x20, ' ', 0x1F);
    memcpy(b + at1, f1, strlen(f1)); if (f2) memcpy(b + at2, f2, n2);
    return b;
}
/** Writes `b` as .z64 (0), .v64 (1: halves swapped) or .n64 (2: words reversed). */
static void dump(const char *rel, const unsigned char *b, size_t size, int order) {
    unsigned char *o = malloc(size); assert(o);
    for (size_t i = 0; i < size; i += 4)
        for (int k = 0; k < 4; k++) o[i + k] = b[i + (order == 1 ? k ^ 1 : order == 2 ? 3 - k : k)];
    FILE *f = fopen(at_root(rel), "wb"); assert(f && fwrite(o, 1, size, f) == size); fclose(f); free(o);
}
static bool same_as(const char *rel, const unsigned char *b, size_t size) {
    FILE *f = fopen(at_root(rel), "rb"); if (!f) return false;
    unsigned char *got = malloc(size + 1); size_t n = fread(got, 1, size + 1, f); fclose(f);
    bool ok = n == size && !memcmp(got, b, size); free(got); return ok;
}
static json_t *requirement(json_t *root, const char *id) {
    json_t *app; size_t i;
    json_array_foreach(json_object_get(root, "apps"), i, app)
        if (!strcmp(json_string_value(json_object_get(app, "id")), id)) return json_array_get(json_object_get(app, "requires"), 0);
    assert(!"app in the catalog"); return NULL;
}
static const char *scan_at(json_t *req) { static char path[300]; return rom_find_scan(req, path, sizeof path, NULL) ? path + strlen(rom_find_root) : NULL; }
#define FOUND(req, rel) do { const char *w = scan_at(req); if (!w || strcmp(w, rel)) { fprintf(stderr, "%s:%d found %s, want %s\n", __FILE__, __LINE__, w ? w : "nothing", rel); abort(); } } while (0)

// A port that opens one fixed name: any name and byte order is copied there as .z64, the player's file stays.
static void fixed_name(json_t *req, const char *folder, const char *reads, size_t size, const unsigned char *rom, const unsigned char *other) {
    char a[200], b[200], c[200], t[200], old[200];
    snprintf(a, sizeof a, "%sMy Dump (USA).v64", folder); snprintf(b, sizeof b, "%sgame.n64", folder);
    snprintf(c, sizeof c, "%sgame.z64", folder); snprintf(t, sizeof t, "%s%s", folder, reads); snprintf(old, sizeof old, "%s.old", t);
    dirs(folder);
    assert(!scan_at(req));
    dump(c, other, size, 0);                                     // another region or revision: never copied
    assert(!scan_at(req) && !exists(t));
    gone(c);
    dump(a, rom, size, 1);
    FOUND(req, t); assert(same_as(t, rom, size) && exists(a));   // converted beside the player's .v64
    FOUND(req, t); assert(!exists(old));                         // already in place: nothing more
    gone(t); gone(a);
    dump(b, rom, size, 2); FOUND(req, t); assert(same_as(t, rom, size)); gone(t); gone(b);
    dump(c, rom, size, 0); FOUND(req, t); assert(same_as(t, rom, size) && same_as(c, rom, size)); gone(t); gone(c);
    dump(t, rom, size, 1);                                       // the port's name, but .v64 inside
    FOUND(req, t); assert(same_as(t, rom, size)); gone(t); gone(old);
    dump(t, other, size, 0); dump(c, rom, size, 0);              // the port's name holds another ROM
    FOUND(req, t); assert(same_as(t, rom, size) && same_as(old, other, size)); gone(t); gone(c);
    dump(t, rom, size, 1); assert(!scan_at(req));                // <name>.old taken: nothing is overwritten
    assert(same_as(old, other, size) && exists(t)); gone(t); gone(old);
}

static void ports(const char *catalog) {
    json_error_t e; json_t *root = json_load_file(catalog, 0, &e); assert(root);
    json_t *app; size_t i, k; json_t *req;
    // Every file a port needs is recognized by the store, not only one exact path.
    json_array_foreach(json_object_get(root, "apps"), i, app)
        json_array_foreach(json_object_get(app, "requires"), k, req)
            if (!rom_find_ruled(req)) { fprintf(stderr, "%s: requirement without detect rules\n", json_string_value(json_object_get(app, "id"))); abort(); }

    // Star Haven reads any name itself, in any byte order, from its folder and a few others.
    req = requirement(root, "starhaven");
    dirs("/3ds/StarHaven"); dirs("/roms/n64");
    size_t pm = 41943040;
    unsigned char *us = n64(pm, 0x3B, "NMQE", 0, NULL, 0), *eu = n64(pm, 0x3B, "NMQP", 0, NULL, 0), *jp = n64(pm, 0x3B, "NMQJ", 0, NULL, 0);
    assert(!scan_at(req));
    put("/3ds/StarHaven/save.fla", 512, 0, NULL);
    dump("/3ds/StarHaven/japan.z64", jp, pm, 0);                 // the port refuses the Japanese ROM
    assert(!scan_at(req));
    dump("/3ds/StarHaven/paper_mario.z64", us, pm, 0);           // reported: any name, not the example
    FOUND(req, "/3ds/StarHaven/paper_mario.z64"); gone("/3ds/StarHaven/paper_mario.z64");
    dump("/3ds/StarHaven/Banjo Kazooie", eu, pm, 1);             // no extension, .v64, Europe
    FOUND(req, "/3ds/StarHaven/Banjo Kazooie"); gone("/3ds/StarHaven/Banjo Kazooie");
    dump("/roms/n64/pm.n64", us, pm, 2);                         // .n64, another folder the port reads
    FOUND(req, "/roms/n64/pm.n64"); gone("/roms/n64/pm.n64"); gone("/3ds/StarHaven/japan.z64");
    free(us), free(eu), free(jp);

    // The N64 recompilations open <folder>/<id>.z64 only; Mario Kart 64 tries three names.
    struct { const char *id, *folder, *reads, *name; size_t size; } fixed[] = {
        {"dk64", "/3ds/DK64/", "DK64.z64", "DONKEY KONG 64", 33554432},
        {"banjo", "/3ds/BanjoRecomp/", "BK.z64", "Banjo-Kazooie", 16777216},
        {"goemon", "/3ds/Goemon64/", "mnsg.us.z64", "MYSTICAL NINJA", 16777216}};
    for (unsigned n = 0; n < 3; n++) {
        unsigned char *rom = n64(fixed[n].size, 0x20, fixed[n].name, 0x3E, "E\0", 2);
        unsigned char *other = n64(fixed[n].size, 0x20, fixed[n].name, 0x3E, n == 1 ? "E\1" : "P\0", 2);  // Banjo 1.1, else Europe
        fixed_name(requirement(root, fixed[n].id), fixed[n].folder, fixed[n].reads, fixed[n].size, rom, other);
        free(rom), free(other);
    }
    req = requirement(root, "mk64");
    unsigned char *mk = n64(12582912, 0x3B, "NKTE", 0, NULL, 0), *mkj = n64(12582912, 0x3B, "NKTJ", 0, NULL, 0);
    fixed_name(req, "/3ds/MK64/", "Mario Kart 64.z64", 12582912, mk, mkj);
    dump("/3ds/MK64/mk64.z64", mk, 12582912, 0);                // a name the port tries: no copy
    FOUND(req, "/3ds/MK64/mk64.z64"); assert(!exists("/3ds/MK64/Mario Kart 64.z64")); gone("/3ds/MK64/mk64.z64");
    free(mk), free(mkj);

    // Engines with their own file names: checked by name and, where it has one, by header.
    struct { const char *id, *folder, *file, *header; } data[] = {
        {"ctrquake", "/3ds/ctrQuake/id1", "/3ds/ctrQuake/id1/pak0.pak", "PACK"},
        {"xash3ds", "/xash3d/valve", "/xash3d/valve/pak0.pak", "PACK"},
        {"worms", "/3ds/worms3ds", "/3ds/worms3ds/assets.worms", NULL},
        {"openlara", "/3ds/OpenLara/DATA", "/3ds/OpenLara/DATA/GYM.PHD", NULL},
        {"openlara", "/3ds/OpenLara/PSXDATA", "/3ds/OpenLara/PSXDATA/GYM.PSX", NULL},
        {"openlara", "/3ds/OpenLara/DATA", "/3ds/OpenLara/DATA/angkor1.tr4", NULL},
        {"butterscotch", "/3ds/butterscotch/Some Game", "/3ds/butterscotch/Some Game/data.win", NULL}};
    for (unsigned n = 0; n < sizeof data / sizeof *data; n++) {
        req = requirement(root, data[n].id);
        dirs(data[n].folder);
        assert(!scan_at(req));
        if (data[n].header) { put(data[n].file, 64, 0, "ZIP!"); assert(!scan_at(req)); }
        put(data[n].file, 64, 0, data[n].header ? data[n].header : "x");
        FOUND(req, data[n].file); gone(data[n].file);
    }
    put("/3ds/butterscotch/data.win", 64, 0, "x");                 // Butterscotch lists folders only
    assert(!scan_at(requirement(root, "butterscotch"))); gone("/3ds/butterscotch/data.win");
    json_decref(root);
}
int main(int argc, char **argv) {
    strcpy(rom_find_root, "romfind-XXXXXX"); assert(mkdtemp(rom_find_root));  // in the runner's temporary folder
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
    if (argc > 1) ports(argv[1]);
    puts("PASS: any accepted name, ignored dot files, sizes, header bytes, CRC with copier header, derived data, exact names, unsafe folders, every catalog requirement ruled; each port's files under any name and byte order, copied where fixed-name ports read them");
}
