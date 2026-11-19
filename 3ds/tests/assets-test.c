// Production assets.c: manifest validation, RomFS-first lookup, bounded pack views,
// installation by size, publication, gzip JSON, wishes and pruning. One scenario per run
// (the registry is loaded once per process, exactly as on the console).
#include <jansson.h>
#include "../source/assets.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static unsigned char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    rewind(f);
    unsigned char *p = malloc(*n + 1);
    assert(p && fread(p, 1, *n, f) == *n);
    fclose(f);
    return p;
}
static void expect(const char *path, const char *want) {
    size_t n = 0;
    unsigned char *got = asset_read(path, &n, 1 << 20);
    assert(got && n == strlen(want) && !memcmp(got, want, n));
    free(got);
}
static void *reader(void *arg) {
    for (int i = 0; i < 200; i++) {
        expect("romfs:/gfx/one.ftx", "first file");
        assert(asset_state("romfs:/gfx/two.ftx", NULL) == ASSET_READY);
    }
    return arg;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *mode = argv[1];
    if (!strcmp(mode, "nomanifest")) {
        // Regression: without packs.json a lookup must not re-enter the registry (deadlock).
        assert(!asset_json("romfs:/missing.json", 0, NULL));
        assert(asset_state("romfs:/gfx/one.ftx", NULL) == ASSET_MISSING && asset_pack_count() == 0);
        expect("romfs:/core.txt", "inside the CIA");
        puts("no manifest");
        return 0;
    }
    if (!strcmp(mode, "invalid")) {
        // Any defect rejects the whole manifest: nothing is half-published.
        assert(asset_pack_count() == 0);
        assert(asset_state("romfs:/gfx/one.ftx", NULL) == ASSET_MISSING);
        assert(!asset_fopen("romfs:/gfx/one.ftx"));
        puts("invalid manifest rejected");
        return 0;
    }
    if (!strcmp(mode, "installed")) {
        // A pack already on the SD at its final name and size is used at once; one of the
        // wrong size is not (it is downloaded again).
        assert(asset_pack_count() == 2);
        assert(asset_pack_installed(0) && !asset_pack_installed(1));
        expect("romfs:/gfx/one.ftx", "first file");
        int pack = -1;
        assert(asset_state("romfs:/reader-data/page", &pack) == ASSET_PENDING && pack == 1);
        asset_pack_prune();
        struct stat st;
        AssetPack p0, p1;
        assert(asset_pack(0, &p0) && asset_pack(1, &p1));
        assert(!stat(p0.path, &st) && !stat(p1.path, &st));  // current names are kept
        assert(stat("packs/old-000000000000.fpk", &st) && stat("packs/x.fpk.part", &st));
        // Many threads read views of the same pack concurrently.
        pthread_t t[4];
        for (int i = 0; i < 4; i++) assert(!pthread_create(&t[i], NULL, reader, NULL));
        for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
        puts("installed packs, pruning and concurrent views");
        return 0;
    }
    assert(!strcmp(mode, "fresh"));
    assert(asset_pack_count() == 2);
    AssetPack p;
    assert(asset_pack(0, &p) && !strcmp(p.id, "banners-1") && p.bytes == 20);
    assert(!strncmp(p.url, "https://raw.githubusercontent.com/", 34));
    assert(!asset_pack(2, &p));
    // RomFS first; core files never touch the registry.
    expect("romfs:/core.txt", "inside the CIA");
    json_t *j = asset_json("romfs:/core.json.gz", 0, NULL);
    assert(json_integer_value(json_object_get(j, "ok")) == 7);
    json_decref(j);
    j = asset_json("romfs:/plain.json", 0, NULL);
    assert(json_is_true(json_object_get(j, "plain")));
    json_decref(j);
    // Before installation: pending, with the pack to ask for.
    int pack = -1;
    assert(asset_state("romfs:/gfx/one.ftx", &pack) == ASSET_PENDING && pack == 0);
    assert(asset_state("romfs:/gfx/nope.ftx", &pack) == ASSET_MISSING && pack == -1);
    assert(asset_state("sdmc:/elsewhere", NULL) == ASSET_MISSING);
    assert(!asset_fopen("romfs:/gfx/one.ftx"));
    assert(asset_take_wanted() == -1);
    asset_want(1);
    asset_want(0);
    assert(asset_take_wanted() == 0 && asset_take_wanted() == -1);
    // Publish the verified file under its final name.
    unsigned generation = asset_generation();
    assert(!asset_pack_installed(0));
    assert(!rename("staged/banners-1.fpk", p.path));
    asset_pack_publish(0);
    assert(asset_pack_installed(0) && asset_generation() != generation);
    expect("romfs:/gfx/one.ftx", "first file");
    expect("romfs:/gfx/two.ftx", "second.ftx");
    assert(asset_state("romfs:/gfx/two.ftx", NULL) == ASSET_READY);
    // A view is bounded to its own bytes: seek, tell, EOF and no neighbour leakage.
    FILE *f = asset_fopen("romfs:/gfx/one.ftx");
    assert(f);
    char buf[64] = {0};
    assert(fread(buf, 1, sizeof buf, f) == 10 && !memcmp(buf, "first file", 10));
    assert(fgetc(f) == EOF && feof(f));
    assert(!fseek(f, 6, SEEK_SET) && ftell(f) == 6 && fgetc(f) == 'f');
    assert(!fseek(f, -2, SEEK_END) && fgetc(f) == 'l');
    assert(fseek(f, 11, SEEK_SET) != 0 || ftell(f) <= 10);
    assert(!fclose(f));
    // The JSON pack entry is gzip-compressed and still readable.
    assert(rename("staged/github-pages-1.fpk", (asset_pack(1, &p), p.path)) == 0);
    asset_pack_publish(1);
    j = asset_json("romfs:/reader-data/page.json", 0, NULL);
    assert(json_integer_value(json_object_get(j, "page")) == 1);
    json_decref(j);
    size_t n;
    unsigned char *raw = slurp(p.path, &n);
    assert(n == p.bytes);
    free(raw);
    unsigned char *big = asset_read("romfs:/reader-data/page", &n, 3);
    assert(!big && n == 0);  // limit honoured
    puts("fresh: pending, publish, bounded views, gzip JSON and wishes");
    return 0;
}
