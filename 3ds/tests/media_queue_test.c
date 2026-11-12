#define MEDIA_HOST_TEST 1
#include "../source/media.c"

#define WAIT_FOR(expr) do { uint64_t end = osGetTime() + 4000; while (!(expr)) { assert(osGetTime() < end); svcSleepThread(1000000); } } while (0)
static unsigned test_count;
static MediaItem item(unsigned i) { MediaItem out; assert(media_item(i, &out)); return out; }
static bool state(unsigned i, MediaState s) { MediaItem out; return media_item(i, &out) && out.state == s; }
static void digest(char hex[65]) {
    unsigned char d[32]; CC_SHA256(payload, sizeof payload, d);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
}
static void manifest(unsigned n) {
    char hash[65]; digest(hash);
    json_t *m = json_object();
    for (unsigned i = 0; i < n; i++) {
        char key[80], url[384]; snprintf(key, sizeof key, "app/video%u", i);
        snprintf(url, sizeof url, i&1?"https://raw.githubusercontent.com/example/port/commit/video%u":"https://github.com/example/video%u", i);
        json_object_set_new(m, key, json_pack("{s:s,s:s,s:i}", "url", url, "sha256", hash, "bytes", (int)sizeof payload));
    }
    assert(!json_dump_file(m, MEDIA_MANIFEST, 0)); json_decref(m);
}
static void finish(void) {
    media_exit();
    assert(!pthread_mutex_destroy(&q.lock));
    memset(&q, 0, sizeof q); memset(&watch, 0, sizeof watch);
}
static void fresh(unsigned n) {
    char tmp[] = "/tmp/forge-media-test-XXXXXX";
    char *dir = mkdtemp(tmp); assert(dir); assert(!chdir(dir));
    calls = slow_started = 0; hold_transfer = bad_payload = test_full = false; fail_mode = 0;
    manifest(n);
}
static void start(void) { assert(media_start()); WAIT_FOR(!media_loading()); }
static void pass(const char *name) { printf("PASS %s\n", name); test_count++; }
int main(void) {
    assert(media_url_allowed("https://raw.githubusercontent.com/EstebanPdN/port/commit/assets/store-media/test.fvid"));
    assert(media_url_allowed("https://github.com/owner/repo/releases/download/v1/test.fvid"));
    assert(!media_url_allowed(NULL)&&!media_url_allowed("http://raw.githubusercontent.com/x"));
    assert(!media_url_allowed("https://raw.githubusercontent.com.evil.test/x")&&!media_url_allowed("https://raw.githubusercontent.com@evil.test/x"));
    assert(!media_url_allowed("https://github.com.evil.test/x")&&!media_url_allowed("file:///tmp/video"));
    for (unsigned i = 0; i < sizeof payload; i++) payload[i] = (unsigned char)(i * 17 + 3);
    fresh(3); hold_transfer = true; start(); WAIT_FOR(LOAD(slow_started));
    assert(media_count() == 3); assert(!media_path_ready(item(0).path));
    char path[256]; assert(!media_ready_path("app", "video0", path, sizeof path));
    assert(media_prioritize("app", "video2"));
    assert(media_request("app", "video0")); media_cancel(); assert(media_poll() == -2);
    __atomic_store_n(&hold_transfer, false, __ATOMIC_RELEASE);
    WAIT_FOR(state(0, MEDIA_COMPLETED) && state(1, MEDIA_COMPLETED) && state(2, MEDIA_COMPLETED));
    assert(calls == 3 && strstr(call_order[1], "video2") && strstr(call_order[2], "video1"));
    assert(media_ready_path("app", "video0", path, sizeof path) && media_path_ready(path));
    assert(!media_ready_path("app", "video0", path, 2));
    assert(media_take_ready() && !media_take_ready());
    assert(media_request("app", "video0") && media_poll() == 1 && *media_path());
    pass("auto-all, priority, partial never ready, navigation cancel preserves queue, ready watcher");
    finish(); calls = 0; start(); WAIT_FOR(state(0, MEDIA_COMPLETED) && state(1, MEDIA_COMPLETED) && state(2, MEDIA_COMPLETED));
    assert(calls == 0); pass("next-launch cache hits are SHA-256 verified without downloads");
    assert(media_clear()); assert(!media_path_ready(item(0).path)); WAIT_FOR(media_clear_poll() == 1);
    assert(access(item(0).path, F_OK) != 0); assert(media_start());
    for (int i = 0; i < 50; i++) { media_pump(); svcSleepThread(1000000); }
    assert(calls == 0 && state(0, MEDIA_CLEARED));
    assert(media_request("app", "video1") && media_poll() == -2);
    assert(media_retry("app", "video1")); WAIT_FOR(state(1, MEDIA_COMPLETED));
    assert(calls == 1 && state(0, MEDIA_CLEARED));
    pass("async clear invalidates and deletes; no start/pump/detail redownload; explicit single retry");
    finish();

    fresh(1); hold_transfer = true; start(); WAIT_FOR(LOAD(slow_started));
    media_set_paused(true); WAIT_FOR(state(0, MEDIA_QUEUED)); assert(item(0).attempts == 0);
    unsigned before = calls; svcSleepThread(50000000); assert(calls == before);
    __atomic_store_n(&hold_transfer, false, __ATOMIC_RELEASE); media_set_paused(false);
    WAIT_FOR(state(0, MEDIA_COMPLETED)); assert(calls == 2 && item(0).attempts == 1);
    pass("installer pause aborts/requeues without spending retry budget and resumes"); finish();

    fresh(1); hold_transfer = true; start(); WAIT_FOR(LOAD(slow_started));
    uint64_t t = now_ms(); assert(media_clear()); assert(now_ms() - t < 100);
    WAIT_FOR(media_clear_poll() == 1); assert(state(0, MEDIA_CLEARED));
    assert(access(item(0).path, F_OK)); char part[272]; snprintf(part, sizeof part, "%s.part", item(0).path); assert(access(part, F_OK));
    pass("clear cancels in-flight transfer without main-thread join or partial residue"); finish();

    fresh(1); fail_mode = 1; start(); WAIT_FOR(state(0, MEDIA_ERROR) && item(0).attempts == 3);
    before = calls; svcSleepThread(150000000); assert(calls == 3 && calls == before);
    assert(!media_path_ready(item(0).path));
    pass("offline failure bounded to three attempts, then stable error"); finish();

    fresh(1); fail_mode = 2; start(); WAIT_FOR(state(0, MEDIA_ERROR)); svcSleepThread(150000000);
    assert(calls == 1 && item(0).attempts == 1 && strstr(item(0).error, "404"));
    pass("permanent HTTP 404 does not spin/retry"); finish();

    fresh(1); bad_payload = true; start(); WAIT_FOR(state(0, MEDIA_ERROR));
    assert(calls == 1 && strstr(item(0).error, "SHA-256") && access(item(0).path, F_OK));
    snprintf(part, sizeof part, "%s.part", item(0).path); assert(access(part, F_OK));
    pass("hash mismatch never publishes; part removed"); finish();

    fresh(1); test_full = true; start(); WAIT_FOR(state(0, MEDIA_ERROR));
    assert(calls == 0 && strstr(item(0).error, "space"));
    pass("SD-full fails before network request"); finish();

    fresh(1); start(); WAIT_FOR(state(0, MEDIA_COMPLETED));
    snprintf(path, sizeof path, "%s", item(0).path); finish();
    FILE *f = fopen(path, "r+b"); assert(f); fputc(99, f); fclose(f);
    calls = 0; start(); WAIT_FOR(state(0, MEDIA_COMPLETED)); assert(calls == 1);
    pass("same-size corrupt cached file is rejected and replaced"); finish();

    fresh(2); media_set_paused(true); start();
    svcSleepThread(30000000); assert(calls == 0 && state(0, MEDIA_QUEUED));
    assert(media_clear()); WAIT_FOR(media_clear_poll() == 1);
    media_retry_all(); media_set_paused(false);
    WAIT_FOR(state(0, MEDIA_COMPLETED) && state(1, MEDIA_COMPLETED));
    assert(calls == 2); pass("pause before start preserved and explicit retry-all resumes cleared entries"); finish();

    fresh(1); hold_transfer = true; start(); WAIT_FOR(LOAD(slow_started));
    assert(!mkdir(MEDIA_CACHE "/blocked.fvid", 0700));
    f = fopen(MEDIA_CACHE "/blocked.fvid/keep", "w"); assert(f); fputs("keep", f); fclose(f);
    assert(media_clear()); WAIT_FOR(media_clear_poll() == -1);
    assert(strstr(media_queue_error(), "could not") && !access(MEDIA_CACHE "/blocked.fvid/keep", F_OK));
    assert(state(0, MEDIA_CLEARED) && !media_path_ready(item(0).path));
    pass("clear reports removal failure without recursively deleting unrelated content"); finish();

    fresh(MEDIA_MAX_ITEMS + 1); start(); assert(media_count() == 0 && strstr(media_queue_error(), "Too many"));
    assert(calls == 0); pass("bounded manifest overflow is explicit error"); finish();
    fresh(1); f = fopen(MEDIA_MANIFEST, "w"); assert(f); fputs("{\"../escape\":{}}", f); fclose(f);
    start(); assert(media_count() == 0 && strstr(media_queue_error(), "key"));
    pass("unsafe manifest path cannot escape cache"); finish();
    printf("%u media queue scenarios passed\n", test_count);
}
