#include "store_update.h"
#include "store_update_policy.h"
#include "network.h"
#include "hbl_launch.h"
#include <mbedtls/sha256.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>

#define UPDATE_BASE "https://raw.githubusercontent.com/EstebanPdN/forge.store/main/builds/3ds/"
static Thread worker;
static int state = UPDATE_CHECKING;
static bool cancelled, homebrew;
static int decision;
static char offered_version[40];
static u64 started_at;
static uint32_t received, total;
static char executable[1024];
static char metadata[16384];
static size_t metadata_size;
static void publish(StoreUpdateState s) { __atomic_store_n(&state, s, __ATOMIC_RELEASE); }
StoreUpdateState store_update_state(void) { return __atomic_load_n(&state, __ATOMIC_ACQUIRE); }
const char *store_update_version(void) { return offered_version; }
bool store_update_decide(bool accept) {
    if (store_update_state() != UPDATE_AVAILABLE) return false;
    int waiting = 0;
    return __atomic_compare_exchange_n(&decision, &waiting, accept ? 1 : -1, false,
                                      __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}
float store_update_progress(void) {
    uint32_t n = __atomic_load_n(&total, __ATOMIC_RELAXED);
    return n ? (float)__atomic_load_n(&received, __ATOMIC_RELAXED) / n : 0;
}
void store_update_bytes(uint32_t *got, uint32_t *all) {
    *got = __atomic_load_n(&received, __ATOMIC_RELAXED);
    *all = __atomic_load_n(&total, __ATOMIC_RELAXED);
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
    // The import AM commits on finish, as FBI and Universal-Updater update themselves; it is
    // finalized only after every byte was written. Never delete the existing title to make room.
    r = AM_StartCiaInstall(MEDIATYPE_SD, &output);
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
    if (!output)
        return false;
    if (!ok || R_FAILED(AM_FinishCiaInstall(output))) {
        AM_CancelCIAInstall(output);
        return false;
    }
    // Done means the title database now holds the downloaded version, not only that AM
    // accepted the bytes: otherwise the next start offers the same update again.
    u64 title = STORE_TITLE_ID;
    AM_TitleInfo installed;
    return R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &title, &installed)) && installed.version == info.version;
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
    snprintf(backup, sizeof backup, "%s.previous", executable);
    // Not a .3dsx: the launcher must not list the old build. The next start removes it.
    if (access(executable, F_OK))
        return false;
    if (!access(backup, F_OK) && unlink(backup))
        return false;
    if (rename(executable, backup))
        return false;
    // libctru cannot atomically overwrite a file on FAT. If power is lost here, the
    // previous build is still on the SD card under the backup name.
    if (rename(staged, executable)) {
        rename(backup, executable);
        return false;
    }
    return true;
}
// Worker, before the check. The running 3DSX may be renamed: its RomFS handle stays open,
// as when an update replaces it. `executable` is read again only after UPDATE_DONE.
static void tidy_homebrew(void) {
    if (!homebrew || strncmp(executable, "sdmc:/", 6) || strlen(executable) > 980)
        return;
    if (store_update_misnamed(executable) && access(STORE_HOME_3DSX, F_OK) && !rename(executable, STORE_HOME_3DSX))
        snprintf(executable, sizeof executable, "%s", STORE_HOME_3DSX);
    char folder[1024], path[1300];
    const char *name = strrchr(executable, '/') + 1;
    snprintf(folder, sizeof folder, "%.*s", (int)(name - executable - 1), executable);
    DIR *d = opendir(folder);
    if (!d)
        return;
    struct dirent *e;
    while (!stopped() && (e = readdir(d)))
        if (store_update_stale_backup(e->d_name, name)) {
            snprintf(path, sizeof path, "%s/%s", folder, e->d_name);
            unlink(path);
        }
    closedir(d);
}
static void run(void *unused) {
    tidy_homebrew();
    if (homebrew && access("romfs:/personal-assets.fme", F_OK) == 0) {
        publish(UPDATE_CURRENT);
        return;
    }
    // Both launchers read update.json from the repository. The GitHub API allows 60
    // requests an hour per connection and the store's own pages use them up, which made
    // the CIA check fail silently (1.4 consoles were not offered 1.4.1).
    char download_url[256];
    snprintf(download_url, sizeof download_url, UPDATE_BASE "%s", homebrew ? "forge-store.3dsx" : "forge-store.cia");
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
    bool newer = false, valid;
    valid = store_update_metadata(root, homebrew, &bytes, expected, &build);
    if (valid) store_update_version_label(root, build, offered_version, sizeof offered_version);
    newer = valid && build > STORE_BUILD;
    json_decref(root);
    if (!valid) {
        publish(UPDATE_OFFLINE);
        return;
    }
    if (!newer) {
        int expected_state = UPDATE_CHECKING;
        __atomic_compare_exchange_n(&state, &expected_state, UPDATE_CURRENT, false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE);
        return;
    }
    // Publish all offer metadata before the main thread renders the prompt. The
    // deadline applies only to checking, never to the user's time to decide.
    int expected_state = UPDATE_CHECKING;
    if (stopped() || !__atomic_compare_exchange_n(&state, &expected_state, UPDATE_AVAILABLE, false,
                                                  __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return;
    while(!stopped() && !__atomic_load_n(&decision,__ATOMIC_ACQUIRE))svcSleepThread(20000000);
    if(stopped() || __atomic_load_n(&decision,__ATOMIC_ACQUIRE)<0){publish(UPDATE_DECLINED);return;}
    bg_wait();
    if(stopped()){publish(UPDATE_DECLINED);return;}
    struct statvfs space;
    if (statvfs("sdmc:/", &space) ||
        (uint64_t)space.f_bavail * space.f_frsize < bytes * 2 + 8 * 1024 * 1024) {
        publish(UPDATE_FAILED);
        return;
    }
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
    c = request(download_url);
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
