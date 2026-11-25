// Resource packs: one manifest in RomFS, immutable pack files on the SD card.
// The registry is built once, on first use, by whichever thread asks first. After that
// only the per-pack "installed" flags and the generation counter change (atomically).
#ifdef ASSETS_HOST_TEST
#include "../tests/assets_host_platform.h"
#else
#include "common.h"
#endif
#include "assets.h"
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <zlib.h>

#ifndef ASSET_MANIFEST
#define ASSET_MANIFEST "romfs:/packs.json"
#endif
#ifndef ASSET_PACK_DIR
#define ASSET_PACK_DIR DATA_DIR "/packs"
#endif
#define ROMFS "romfs:/"
#define MAX_FILES 4096
#define TABLE 8192  // power of two, at least twice MAX_FILES

typedef struct {
    const char *path;  // relative to RomFS, inside `names`
    unsigned pack, offset, size;
} Entry;
static struct {
    LightLock lock;
    bool lock_ready, loaded;
    unsigned npacks, nfiles;
    AssetPack packs[ASSET_PACKS_MAX];
    char file[ASSET_PACKS_MAX][96];
    bool installed[ASSET_PACKS_MAX];
    Entry *files;
    int *table;
    char *names;
    unsigned generation;
} R;

static unsigned name_hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}
static const Entry *lookup(const char *rel) {
    if (!R.table) return NULL;
    for (unsigned k = name_hash(rel) & (TABLE - 1);; k = (k + 1) & (TABLE - 1)) {
        int i = R.table[k];
        if (i < 0) return NULL;
        if (!strcmp(R.files[i].path, rel)) return &R.files[i];
    }
}
static bool safe_name(const char *s, size_t max, bool slashes) {
    if (!s || !*s || strlen(s) > max || strstr(s, "..")) return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == '-' || *s == '_' || *s == '.' || (slashes && *s == '/')))
            return false;
    return true;
}
static bool hex64(const char *s) {
    if (!s || strlen(s) != 64) return false;
    for (int i = 0; i < 64; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}
static size_t gz_json(void *buffer, size_t length, void *data) {
    int n = gzread((gzFile)data, buffer, length > 65536 ? 65536 : (unsigned)length);
    return n < 0 ? (size_t)-1 : (size_t)n;
}
// A real file only (never the registry: the manifest is read while it is being built).
static json_t *json_file(const char *path, size_t flags, json_error_t *error) {
    gzFile z = path ? gzopen(path, "rb") : NULL;  // gzread passes plain files through
    if (!z) return NULL;
    gzbuffer(z, 32768);
    json_t *j = json_load_callback(gz_json, z, flags, error);
    gzclose(z);
    return j;
}
static void unload(void) {
    free(R.files); free(R.table); free(R.names);
    R.files = NULL; R.table = NULL; R.names = NULL;
    R.npacks = R.nfiles = 0;
}
// Validates everything before publishing: a damaged manifest leaves no packs at all.
static void load(void) {
    json_t *root = json_file(ASSET_MANIFEST, JSON_REJECT_DUPLICATES, NULL);
    json_t *packs = json_object_get(root, "packs");
    size_t total = 0, names = 0;
    size_t index;
    json_t *p;
    if (json_integer_value(json_object_get(root, "format")) != 1 || !json_is_array(packs) ||
        json_array_size(packs) > ASSET_PACKS_MAX) goto fail;
    json_array_foreach(packs, index, p) {
        json_t *files = json_object_get(p, "files");
        if (!json_is_array(files)) goto fail;
        total += json_array_size(files);
        size_t i; json_t *f;
        json_array_foreach(files, i, f) names += strlen(json_string_value(json_array_get(f, 0)) ?: "") + 1;
    }
    if (!total || total > MAX_FILES) goto fail;
    R.files = calloc(total, sizeof *R.files);
    R.table = malloc(TABLE * sizeof *R.table);
    R.names = malloc(names);
    if (!R.files || !R.table || !R.names) goto fail;
    for (unsigned k = 0; k < TABLE; k++) R.table[k] = -1;
    char *at = R.names;
    json_array_foreach(packs, index, p) {
        AssetPack *pack = &R.packs[index];
        const char *id = json_string_value(json_object_get(p, "id"));
        const char *file = json_string_value(json_object_get(p, "file"));
        const char *url = json_string_value(json_object_get(p, "url"));
        const char *sha = json_string_value(json_object_get(p, "sha256"));
        json_t *bytes = json_object_get(p, "bytes");
        json_int_t size = json_integer_value(bytes);
        if (!safe_name(id, 48, false) || !safe_name(file, 95, false) || !url || strlen(url) >= sizeof pack->url ||
            strncmp(url, "https://raw.githubusercontent.com/", 34) || !hex64(sha) || !json_is_integer(bytes) ||
            size < 1 || size > 64 * 1024 * 1024)
            goto fail;
        snprintf(pack->id, sizeof pack->id, "%s", id);
        snprintf(pack->url, sizeof pack->url, "%s", url);
        snprintf(pack->sha256, sizeof pack->sha256, "%s", sha);
        snprintf(pack->path, sizeof pack->path, ASSET_PACK_DIR "/%s", file);
        snprintf(R.file[index], sizeof R.file[index], "%s", file);
        pack->bytes = (unsigned)size;
        size_t i; json_t *f;
        json_array_foreach(json_object_get(p, "files"), i, f) {
            const char *rel = json_string_value(json_array_get(f, 0));
            json_int_t offset = json_integer_value(json_array_get(f, 1)), length = json_integer_value(json_array_get(f, 2));
            if (!safe_name(rel, 160, true) || offset < 0 || length < 1 || offset + length > size || lookup(rel)) goto fail;
            size_t n = strlen(rel) + 1;
            memcpy(at, rel, n);
            Entry *e = &R.files[R.nfiles];
            *e = (Entry){at, (unsigned)index, (unsigned)offset, (unsigned)length};
            at += n;
            unsigned k = name_hash(rel) & (TABLE - 1);
            while (R.table[k] >= 0) k = (k + 1) & (TABLE - 1);
            R.table[k] = (int)R.nfiles++;
        }
        // A pack is published only after its SHA-256 matched, under a name that contains
        // that hash, so a file of the right size at the final path is trusted.
        struct stat st;
        R.installed[index] = !stat(pack->path, &st) && S_ISREG(st.st_mode) && st.st_size == size;
    }
    R.npacks = (unsigned)json_array_size(packs);
    json_decref(root);
    return;
fail:
    unload();
    json_decref(root);
}
static void ensure(void) {
    if (__atomic_load_n(&R.loaded, __ATOMIC_ACQUIRE)) return;
    // The first caller can be any worker; a static initializer is not portable to LightLock.
    static int once;
    if (__atomic_exchange_n(&once, 1, __ATOMIC_ACQ_REL) == 0) {
        LightLock_Init(&R.lock);
        __atomic_store_n(&R.lock_ready, true, __ATOMIC_RELEASE);
    }
    while (!__atomic_load_n(&R.lock_ready, __ATOMIC_ACQUIRE)) svcSleepThread(100000);
    LightLock_Lock(&R.lock);
    if (!R.loaded) {
        load();
        __atomic_store_n(&R.loaded, true, __ATOMIC_RELEASE);
    }
    LightLock_Unlock(&R.lock);
}
static const char *relative(const char *path) {
    return path && !strncmp(path, ROMFS, strlen(ROMFS)) ? path + strlen(ROMFS) : NULL;
}

// ---- views inside a pack ------------------------------------------------------------------
// newlib and BSD use funopen. The Linux host harness uses glibc's fopencookie.
#if defined(__LARGE64_FILES) && defined(__3DS__)
typedef _READ_WRITE_BUFSIZE_TYPE view_size;
typedef _fpos64_t view_pos;
#elif defined(__3DS__)
typedef _READ_WRITE_BUFSIZE_TYPE view_size;
typedef fpos_t view_pos;
#elif defined(__GLIBC__)
typedef size_t view_size;
typedef off64_t view_pos;
#else
typedef int view_size;
typedef fpos_t view_pos;
#endif
#ifdef __GLIBC__
typedef ssize_t view_read_result;
#else
typedef int view_read_result;
#endif
typedef struct {
    FILE *file;
    long base, size, pos;
} View;
static view_read_result view_read(void *cookie, char *buf, view_size length) {
    View *v = cookie;
    long left = v->size - v->pos, n = length > (view_size)65536 ? 65536 : (long)length;
    if (n > left) n = left;
    if (n <= 0) return 0;
    if (fseek(v->file, v->base + v->pos, SEEK_SET)) return -1;
    size_t got = fread(buf, 1, (size_t)n, v->file);
    v->pos += (long)got;
    return got || !ferror(v->file) ? (int)got : -1;
}
static view_pos view_seek(void *cookie, view_pos offset, int whence) {
    View *v = cookie;
    long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? v->pos : whence == SEEK_END ? v->size : -1;
    if (base < 0) { errno = EINVAL; return -1; }
    long target = base + (long)offset;
    if (target < 0 || target > v->size) { errno = EINVAL; return -1; }
    v->pos = target;
    return (view_pos)target;
}
static int view_close(void *cookie) {
    View *v = cookie;
    int r = fclose(v->file);
    free(v);
    return r;
}
#ifdef __GLIBC__
static int cookie_seek(void *cookie, off64_t *offset, int whence) {
    view_pos pos = view_seek(cookie, *offset, whence);
    if (pos < 0) return -1;
    *offset = pos;
    return 0;
}
#endif
static FILE *pack_view(const Entry *e) {
    View *v = malloc(sizeof *v);
    if (!v) return NULL;
    *v = (View){fopen(R.packs[e->pack].path, "rb"), e->offset, e->size, 0};
    if (!v->file) { free(v); return NULL; }
#ifdef __GLIBC__
    cookie_io_functions_t io = {.read = view_read, .seek = cookie_seek, .close = view_close};
    FILE *f = fopencookie(v, "r", io);
#else
    FILE *f = funopen(v, view_read, NULL, view_seek, view_close);
#endif
    if (!f) { fclose(v->file); free(v); }
    return f;
}

FILE *asset_fopen(const char *path) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    const char *rel = relative(path);
    if (f || !rel) return f;
    ensure();
    const Entry *e = lookup(rel);
    if (!e || !__atomic_load_n(&R.installed[e->pack], __ATOMIC_ACQUIRE)) return NULL;
    return pack_view(e);
}
unsigned char *asset_read(const char *path, size_t *size, size_t limit) {
    *size = 0;
    FILE *f = asset_fopen(path);
    if (!f) return NULL;
    unsigned char *data = NULL;
    long n = fseek(f, 0, SEEK_END) ? -1 : ftell(f);
    if (n > 0 && (size_t)n <= limit && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n)) &&
        fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) *size = (size_t)n;
    return data;
}
AssetState asset_state(const char *path, int *pack) {
    if (pack) *pack = -1;
    struct stat st;
    if (path && !stat(path, &st) && S_ISREG(st.st_mode)) return ASSET_READY;
    const char *rel = relative(path);
    if (!rel) return ASSET_MISSING;
    ensure();
    const Entry *e = lookup(rel);
    if (!e) return ASSET_MISSING;
    if (__atomic_load_n(&R.installed[e->pack], __ATOMIC_ACQUIRE)) return ASSET_READY;
    if (pack) *pack = (int)e->pack;
    return ASSET_PENDING;
}
json_t *asset_json(const char *path, size_t flags, json_error_t *error) {
    // RomFS JSON may be gzip-compressed; gzread passes plain files through unchanged.
    struct stat st;
    if (path && !stat(path, &st)) return json_file(path, flags, error);
    // Inside a pack: read it whole, then inflate when it is gzip.
    size_t n = 0;
    unsigned char *raw = asset_read(path, &n, 8u * 1024u * 1024u);
    json_t *j = NULL;
    if (raw && n > 18 && raw[0] == 0x1f && raw[1] == 0x8b) {
        unsigned isize = raw[n - 4] | raw[n - 3] << 8 | raw[n - 2] << 16 | (unsigned)raw[n - 1] << 24;
        unsigned char *plain = isize && isize <= 32u * 1024u * 1024u ? malloc(isize) : NULL;
        z_stream s = {.next_in = raw, .avail_in = (uInt)n, .next_out = plain, .avail_out = isize};
        if (plain && inflateInit2(&s, 16 + MAX_WBITS) == Z_OK) {
            if (inflate(&s, Z_FINISH) == Z_STREAM_END && s.total_out == isize)
                j = json_loadb((const char *)plain, isize, flags, error);
            inflateEnd(&s);
        }
        free(plain);
    } else if (raw) {
        j = json_loadb((const char *)raw, n, flags, error);
    }
    free(raw);
    return j;
}
unsigned asset_generation(void) { return __atomic_load_n(&R.generation, __ATOMIC_ACQUIRE); }
static int wanted = -1;
void asset_want(int pack) { if (pack >= 0) __atomic_store_n(&wanted, pack, __ATOMIC_RELEASE); }
int asset_take_wanted(void) { return __atomic_exchange_n(&wanted, -1, __ATOMIC_ACQ_REL); }

unsigned asset_pack_count(void) { ensure(); return R.npacks; }
bool asset_pack(unsigned index, AssetPack *out) {
    ensure();
    if (index >= R.npacks || !out) return false;
    *out = R.packs[index];
    return true;
}
bool asset_pack_installed(unsigned index) {
    ensure();
    return index < R.npacks && __atomic_load_n(&R.installed[index], __ATOMIC_ACQUIRE);
}
void asset_pack_publish(unsigned index) {
    ensure();
    if (index >= R.npacks) return;
    __atomic_store_n(&R.installed[index], true, __ATOMIC_RELEASE);
    __atomic_add_fetch(&R.generation, 1, __ATOMIC_ACQ_REL);
}
void asset_pack_prune(void) {
    ensure();
    DIR *d = opendir(ASSET_PACK_DIR);
    if (!d) return;
    struct dirent *e;
    char doomed[16][128];
    unsigned n = 0;
    while ((e = readdir(d)) && n < 16) {
        if (e->d_name[0] == '.') continue;
        bool keep = false;
        for (unsigned i = 0; i < R.npacks && !keep; i++) keep = !strcmp(e->d_name, R.file[i]);
        size_t len = strlen(e->d_name);
        if (!keep && len < sizeof doomed[n]) memcpy(doomed[n++], e->d_name, len + 1);
    }
    closedir(d);
    // Deleting while iterating is unreliable on the 3DS SD; a later launch prunes the rest.
    for (unsigned i = 0; i < n; i++) {
        char path[sizeof ASSET_PACK_DIR + sizeof doomed[i] + 1];
        snprintf(path, sizeof path, ASSET_PACK_DIR "/%.127s", doomed[i]);
        remove(path);
    }
}
