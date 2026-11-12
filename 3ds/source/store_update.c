#include "store_update.h"
#include "store_update_policy.h"
#include "network.h"
#include "hbl_launch.h"
#include <mbedtls/sha256.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define UPDATE_BASE "https://raw.githubusercontent.com/EstebanPdN/forge.store/main/builds/3ds/"
static Thread worker;
static int state = UPDATE_CHECKING;
static bool cancelled, homebrew;
static u64 started_at;
static uint32_t received, total;
static char executable[1024];
static char metadata[16384];
static size_t metadata_size;
static void publish(StoreUpdateState s) { __atomic_store_n(&state, s, __ATOMIC_RELEASE); }
StoreUpdateState store_update_state(void) { return __atomic_load_n(&state, __ATOMIC_ACQUIRE); }
float store_update_progress(void) {
    uint32_t n = __atomic_load_n(&total, __ATOMIC_RELAXED);
    return n ? (float)__atomic_load_n(&received, __ATOMIC_RELAXED) / n : 0;
}
static bool stopped(void) { return __atomic_load_n(&cancelled, __ATOMIC_ACQUIRE); }
void store_update_poll(void) {
    int expected = UPDATE_CHECKING;
    if (osGetTime() - started_at >= 5000 &&
        __atomic_compare_exchange_n(&state, &expected, UPDATE_OFFLINE, false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE))
        __atomic_store_n(&cancelled, true, __ATOMIC_RELEASE);
}
static int progress(void *p, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    bg_wait();
    return stopped();
}
static size_t receive_metadata(char *p, size_t size, size_t count, void *ctx) {
    bg_wait();
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t n = size * count;
    if (stopped() || n > sizeof metadata - 1 - metadata_size)
        return 0;
    memcpy(metadata + metadata_size, p, n);
    metadata_size += n;
    metadata[metadata_size] = 0;
    return n;
}
typedef struct {
    FILE *file;
    mbedtls_sha256_context hash;
    uint64_t bytes, limit;
    bool failed;
} Download;
static size_t receive_file(char *p, size_t size, size_t count, void *ctx) {
    bg_wait();
    Download *d = ctx;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t n = size * count;
    if (stopped() || n > d->limit - d->bytes)
        return 0;
    if (fwrite(p, 1, n, d->file) != n || mbedtls_sha256_update_ret(&d->hash, (unsigned char *)p, n)) {
        d->failed = true;
        return 0;
    }
    d->bytes += n;
    __atomic_store_n(&received, (uint32_t)d->bytes, __ATOMIC_RELAXED);
    return n;
}
static CURL *request(const char *url) {
    CURL *c = curl_easy_init();
    if (!c)
        return NULL;
    network_options(c);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 2500L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    return c;
}
static bool install_cia(const char *path, uint64_t bytes) {
    Handle input = 0, output = 0;
    AM_TitleInfo info;
    if (R_FAILED(FSUSER_OpenFileDirectly(&input, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
                                         fsMakePath(PATH_ASCII, path + 5), FS_OPEN_READ, 0)))
        return false;
    Result r = AM_GetCiaFileInfo(MEDIATYPE_SD, &info, input);
    FSFILE_Close(input);
    u64 running = 0;
    APT_GetProgramID(&running);
    if (R_FAILED(r) || info.titleID != STORE_TITLE_ID || running != STORE_TITLE_ID || stopped())
        return false;
    struct statvfs space;
    if (statvfs("sdmc:/", &space) || (uint64_t)space.f_bavail * space.f_frsize < bytes + 8 * 1024 * 1024)
        return false;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    unsigned char *buffer = malloc(65536);
    if (!buffer) {
        fclose(f);
        return false;
    }
    // AM's overwrite transaction is finalized only after every byte was written.
    // Never delete the existing title to make room for an update.
    r = AM_StartCiaInstallOverwrite(&output, MEDIATYPE_SD);
    bool ok = R_SUCCEEDED(r);
    uint64_t offset = 0;
    while (ok && offset < bytes && !stopped()) {
        bg_wait();
        size_t n = fread(buffer, 1, (size_t)fmin(65536, bytes - offset), f);
        u32 written = 0;
        if (!n) {
            ok = false;
            break;
        }
        r = FSFILE_Write(output, &written, offset, buffer, n, 0);
        ok = R_SUCCEEDED(r) && written == n;
        offset += written;
        __atomic_store_n(&received, (uint32_t)offset, __ATOMIC_RELAXED);
    }
    ok = ok && !stopped() && !ferror(f) && offset == bytes;
    fclose(f);
    free(buffer);
    if (output) {
        if (ok)
            ok = R_SUCCEEDED(AM_FinishCiaInstall(output));
        if (!ok)
            AM_CancelCIAInstall(output);
    }
    return ok;
}
static bool install_homebrew(const char *staged) {
    // Only replace the actual launched executable, never an inferred SD path.
    if (strncmp(executable, "sdmc:/", 6) || strstr(executable, "..") || strlen(executable) > 980)
        return false;
    const char *ext = strrchr(executable, '.');
    if (!ext || strcmp(ext, ".3dsx"))
        return false;
    unsigned char signature[4];
    FILE *f = fopen(staged, "rb");
    if (!f)
        return false;
    bool ok = fread(signature, 1, 4, f) == 4 && !memcmp(signature, "3DSX", 4);
    fclose(f);
    if (!ok || stopped())
        return false;
    char backup[1024];
    snprintf(backup, sizeof backup, "%.*s.forge-previous.3dsx", (int)(strlen(executable) - 5), executable);
    // A stale backup is retained until another complete verified update is available.
    if (access(executable, F_OK))
        return false;
    if (!access(backup, F_OK) && unlink(backup))
        return false;
    if (rename(executable, backup))
        return false;
    // libctru cannot atomically overwrite a file on FAT. Keep a launchable backup
    // until the following successful update, including if power is lost here.
    if (rename(staged, executable)) {
        rename(backup, executable);
        return false;
    }
    return true;
}
static void run(void *unused) {
    CURL *c = request(UPDATE_BASE "update.json");
    if (!c) {
        publish(UPDATE_OFFLINE);
        return;
    }
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 4500L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive_metadata);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || stopped()) {
        publish(UPDATE_OFFLINE);
        return;
    }
    json_t *root = json_loadb(metadata, metadata_size, JSON_REJECT_DUPLICATES, NULL);
    uint64_t bytes = 0;
    int64_t build = 0;
    char expected[65];
    bool valid = store_update_metadata(root, homebrew, &bytes, expected, &build);
    json_decref(root);
    if (!valid) {
        publish(UPDATE_OFFLINE);
        return;
    }
    if (build <= STORE_BUILD) {
        int expected_state = UPDATE_CHECKING;
        __atomic_compare_exchange_n(&state, &expected_state, UPDATE_CURRENT, false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE);
        return;
    }
    struct statvfs space;
    if (statvfs("sdmc:/", &space) ||
        (uint64_t)space.f_bavail * space.f_frsize < bytes * 2 + 8 * 1024 * 1024) {
        publish(UPDATE_FAILED);
        return;
    }
    // A timed-out check can never begin installing later, after the intro opened.
    int expected_state = UPDATE_CHECKING;
    if (stopped() || !__atomic_compare_exchange_n(&state, &expected_state, UPDATE_DOWNLOADING, false,
                                                  __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return;
    const char *staged = DATA_DIR "/store-update.part";
    Download d = {.limit = bytes};
    d.file = fopen(staged, "wb");
    if (!d.file) {
        publish(UPDATE_FAILED);
        return;
    }
    __atomic_store_n(&total, (uint32_t)bytes, __ATOMIC_RELAXED);
    publish(UPDATE_DOWNLOADING);
    mbedtls_sha256_init(&d.hash);
    mbedtls_sha256_starts_ret(&d.hash, 0);
    c = request(homebrew ? UPDATE_BASE "forge-store.3dsx" : UPDATE_BASE "forge-store.cia");
    rc = CURLE_FAILED_INIT;
    if (c) {
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 900L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive_file);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
        rc = curl_easy_perform(c);
        curl_easy_cleanup(c);
    }
    unsigned char digest[32];
    char actual[65];
    int hash_rc = mbedtls_sha256_finish_ret(&d.hash, digest);
    mbedtls_sha256_free(&d.hash);
    bool flushed = fflush(d.file) == 0;
    if (flushed)
        flushed = fsync(fileno(d.file)) == 0;
    if (fclose(d.file))
        flushed = false;
    for (int i = 0; i < 32; i++)
        snprintf(actual + 2 * i, 3, "%02x", digest[i]);
    bool ok = rc == CURLE_OK && !hash_rc && !d.failed && flushed && !stopped() && d.bytes == bytes &&
              !strcmp(actual, expected);
    if (ok) {
        __atomic_store_n(&received, 0, __ATOMIC_RELAXED);
        publish(UPDATE_INSTALLING);
        ok = homebrew ? install_homebrew(staged) : install_cia(staged, bytes);
    }
    unlink(staged);
    publish(ok ? UPDATE_DONE : UPDATE_FAILED);
}
void store_update_start(const char *path) {
    started_at = osGetTime();
    homebrew = envIsHomebrew();
    if (path)
        snprintf(executable, sizeof executable, "%s", path);
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    worker = threadCreate(run, NULL, 64 * 1024, priority + 1, -2, false);
    if (!worker)
        publish(UPDATE_OFFLINE);
}
void store_update_stop(void) { __atomic_store_n(&cancelled, true, __ATOMIC_RELEASE); }
void store_update_exit(void) {
    store_update_stop();
    thread_reap(&worker);
}
void store_update_restart(void) {
    if (homebrew)
        hbl_launch(executable);
    else
        aptSetChainloader(STORE_TITLE_ID, MEDIATYPE_SD);
}
