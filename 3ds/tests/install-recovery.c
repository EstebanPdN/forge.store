// Power loss at every point of real installations, then recovery from the SD alone.
// Each scenario runs the production installer (install.c) in a child process that dies
// just before its Nth SD/AM mutation (rename, remove, mkdir, rmdir, file write, AM commit,
// record write). Scenarios also run with the record commit or AM failing, so the cut can
// land inside the installer's own rollback, and a second cut lands at every mutation of the
// recovery itself. Afterwards the parent recovers and requires:
//   * the staging folder is gone and nothing was set aside,
//   * files, CIA title and installed record are exactly the previous version or exactly the
//     new one (an update over a title installed without forge.store may also end complete
//     but unrecorded: its previous package does not exist to go back to),
//   * a user's own file is never touched,
//   * the next installation succeeds.
// A failed installation is also retried in the same process, with no restart in between.
#define FORGE_INSTALL_HOST_TEST
#include "../source/install.c"
#include <assert.h>
#include <ftw.h>
#include <sys/wait.h>

static bool path_exists(const char *p) { struct stat st; return !stat(p, &st); }
static void svcSleepThread(u64 ns) { if (host_auto_ack) install_poll(NULL); }

#define REAL(f) (f)
#undef fwrite
#undef rename
#undef remove
#undef rmdir
#undef mkdir
#undef fopen

static void put(const char *p, const char *s) {
    FILE *f = fopen(p, "wb");
    assert(f);
    assert(fwrite(s, 1, strlen(s), f) == strlen(s));
    assert(!fclose(f));
}
static int rm_entry(const char *p, const struct stat *st, int flag, struct FTW *ftw) { (void)st; (void)flag; (void)ftw; return remove(p); }
static void rm_rf(const char *p) { nftw(p, rm_entry, 16, FTW_DEPTH | FTW_PHYS); }
static void mkdir_p(const char *p) {
    char b[600];
    snprintf(b, sizeof b, "%s", p);
    for (char *s = b + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(b, 0777); *s = '/'; }
    mkdir(b, 0777);
}

// ---- state: every file and folder on the fixture SD, the title and the app's record ----
static char *state_buf;
static size_t state_len, state_cap;
static void state_add(const char *line) {
    size_t n = strlen(line);
    if (state_len + n + 2 > state_cap) {
        state_cap = (state_len + n + 2) * 2;
        state_buf = realloc(state_buf, state_cap);
        assert(state_buf);
    }
    memcpy(state_buf + state_len, line, n);
    state_len += n;
    state_buf[state_len++] = '\n';
    state_buf[state_len] = 0;
}
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static char *lines[4096];
static int nlines;
static int walk(const char *p, const struct stat *st, int flag, struct FTW *ftw) {
    (void)ftw;
    if (!strncmp(p, DATA_DIR "/installed.json", strlen(DATA_DIR "/installed.json"))) return 0;
    char line[1024];
    if (flag == FTW_D || flag == FTW_DP) snprintf(line, sizeof line, "D %s", p);
    else {
        char buf[256] = {0};
        FILE *f = fopen(p, "rb");
        size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
        if (f) fclose(f);
        buf[n] = 0;
        for (char *c = buf; *c; c++) if (*c == '\n') *c = '|';
        size_t len = strlen(p);
        // An inventory carries a time-based transaction id: compare its presence, not its bytes.
        if (len > 14 && !strcmp(p + len - 14, "/recovery.json")) snprintf(buf, sizeof buf, "<inventory>");
        snprintf(line, sizeof line, "F %s = %s", p, buf);
    }
    (void)st;
    lines[nlines++] = strdup(line);
    return 0;
}
static char *snapshot_state(const char *app) {
    state_len = 0;
    if (state_buf) state_buf[0] = 0;
    nlines = 0;
    nftw("sdmc:", walk, 16, FTW_PHYS);
    qsort(lines, nlines, sizeof *lines, cmp_str);
    for (int i = 0; i < nlines; i++) { state_add(lines[i]); free(lines[i]); }
    host_am_load();
    char line[128];
    snprintf(line, sizeof line, "T %016llx v%u", (unsigned long long)host_title, host_title ? host_version : 0);
    state_add(line);
    json_t *all = installed_saved();
    const char *version = json_string_value(json_object_get(json_object_get(all, app), "version"));
    snprintf(line, sizeof line, "R %s", version ? version : "-");
    state_add(line);
    json_decref(all);
    return strdup(state_buf ? state_buf : "");
}

// ---- scenarios -----------------------------------------------------------------------
typedef enum { S_3DSX_UPDATE, S_3DSX_FRESH, S_CIA_FRESH, S_CIA_UPDATE, S_CIA_EXTERNAL, S_UNINSTALL, S_COUNT } Kind;
static const char *NAMES[] = {"3DSX update", "3DSX first install", "CIA first install", "CIA update (own package)",
                              "CIA update over an FBI install", "uninstall (3DSX + CIA)"};
#define TITLE 0x000400000ABCD000ULL
#define CACHE DATA_DIR "/cia-cache/000400000abcd000.cia"
static App app = {.id = "demo", .repo = "example/demo"};

static void tar(const char *file, const char *const *names, const char *const *data, int n) {
    struct archive *a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    assert(archive_write_open_filename(a, file) == ARCHIVE_OK);
    for (int i = 0; i < n; i++) {
        struct archive_entry *e = archive_entry_new();
        archive_entry_set_pathname(e, names[i]);
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_entry_set_size(e, (la_int64_t)strlen(data[i]));
        assert(archive_write_header(a, e) == ARCHIVE_OK);
        assert(archive_write_data(a, data[i], strlen(data[i])) == (la_ssize_t)strlen(data[i]));
        archive_entry_free(e);
    }
    assert(archive_write_close(a) == ARCHIVE_OK);
    archive_write_free(a);
}

static void records(json_t *record) {
    json_t *all = json_object();
    if (record) json_object_set(all, "demo", record);
    assert(!json_dump_file(all, DATA_DIR "/installed.json", 0));
    json_decref(all);
}
static void am(u64 title, unsigned version) {
    host_title = title;
    host_version = (uint16_t)version;
    host_am_save();
}

// The installed state forge.store leaves behind (used by the uninstall scenario).
static json_t *installed_fixture(void) {
    mkdir_p("sdmc:/3ds/Demo");
    mkdir_p(DATA_DIR "/cia-cache");
    put("sdmc:/3ds/Demo/demo.3dsx", "installed-3dsx");
    put(CACHE, "CIA:000400000abcd000:1:installed");
    am(TITLE, 1);
    char h1[65], h2[65];
    JobStep step = {0};
    volatile bool no = false;
    assert(sha256_file("sdmc:/3ds/Demo/demo.3dsx", h1, &step, &no) && sha256_file(CACHE, h2, &step, &no));
    json_t *r = json_pack("{s:s,s:s,s:[s,s,s],s:[s],s:{s:s,s:s}}", "version", "v1", "transaction", "t-old", "files",
                          "sdmc:/3ds/Demo/demo.3dsx", CACHE, "title:000400000abcd000", "dirs", "sdmc:/3ds/Demo",
                          "hashes", "sdmc:/3ds/Demo/demo.3dsx", h1, CACHE, h2);
    return r;
}

static json_t *script(Kind k) {
    switch (k) {
    case S_3DSX_UPDATE:
    case S_3DSX_FRESH:
        return json_pack("[{s:s,s:s},{s:s,s:s},{s:s,s:s,s:s}]", "type", "mkdir", "directory", "/3ds/Demo/", "type", "mkdir",
                         "directory", "/3ds/Demo/mods/", "type", "extractFile", "file", "/source.tar", "output", "/3ds/");
    default:
        return json_pack("[{s:s,s:s},{s:s,s:s,s:s,s:s},{s:s,s:s,s:s,s:s},{s:s,s:s},{s:s,s:s}]", "type", "mkdir",
                         "directory", "/3ds/Demo/", "type", "extractFile", "file", "/source.tar", "input", "^Demo/data\\.bin",
                         "output", "/3ds/Demo/data.bin", "type", "extractFile", "file", "/source.tar", "input", "^pkg\\.cia",
                         "output", "%TMP%/pkg.cia", "type", "installCia", "file", "%TMP%/pkg.cia", "type", "deleteFile",
                         "file", "%TMP%/pkg.cia");
    }
}

static void fixture(Kind k) {
    rm_rf("sdmc:");
    remove("am-state.json");
    mkdir_p(DATA_DIR);
    am(0, 0);
    records(NULL);
    put("sdmc:/user-save.dat", "my-progress");  // never touched by anything
    if (k == S_3DSX_UPDATE || k == S_3DSX_FRESH) {
        const char *names[] = {"Demo/demo.3dsx", "Demo/data.bin", "Demo/extra.bin"};
        const char *data[] = {"new-3dsx", "new-data", "new-extra"};
        tar("sdmc:/source.tar", names, data, 3);
    } else {
        const char *names[] = {"Demo/data.bin", "pkg.cia"};
        const char *data[] = {"new-data", "CIA:000400000abcd000:2:new-package"};
        tar("sdmc:/source.tar", names, data, 2);
    }
    if (k == S_3DSX_UPDATE) {
        mkdir_p("sdmc:/3ds/Demo");
        put("sdmc:/3ds/Demo/demo.3dsx", "old-3dsx");
        put("sdmc:/3ds/Demo/data.bin", "old-data");
        put("sdmc:/3ds/Demo/save.dat", "user-data");
        json_t *r = json_pack("{s:s,s:s}", "version", "v1", "transaction", "t-old");
        records(r);
        json_decref(r);
    }
    if (k == S_CIA_UPDATE) {
        mkdir_p("sdmc:/3ds/Demo");
        mkdir_p(DATA_DIR "/cia-cache");
        put("sdmc:/3ds/Demo/data.bin", "old-data");
        put(CACHE, "CIA:000400000abcd000:1:old-package");
        am(TITLE, 1);
        json_t *r = json_pack("{s:s,s:s,s:[s,s]}", "version", "v1", "transaction", "t-old", "files", CACHE,
                              "title:000400000abcd000");
        records(r);
        json_decref(r);
    }
    if (k == S_CIA_EXTERNAL) am(TITLE, 1);  // installed with FBI: no package, no record
    if (k == S_UNINSTALL) {
        json_t *r = installed_fixture();
        records(r);
        json_decref(r);
    }
}

static Job *start(Kind k) {
    memset(jobs, 0, sizeof jobs);
    memset(&tx, 0, sizeof tx);
    njobs = 1;
    Job *j = &jobs[0];
    json_decref(app.j);
    app.j = json_loads("{\"detect\":{\"ids\":[703696]}}", 0, NULL);  // 0xABCD0
    j->app = &app;
    snprintf(j->tag, sizeof j->tag, "v2");
    if (k == S_UNINSTALL) {
        j->uninstall = true;
        json_t *all = installed_saved();
        j->record = json_deep_copy(json_object_get(all, "demo"));
        json_decref(all);
        return j;
    }
    j->script = script(k);
    size_t i;
    json_t *s;
    json_array_foreach(j->script, i, s) j->steps[j->nsteps++].raw = s;
    j->known_titles[0] = TITLE;
    // The catalog lists this title for the app (my ports' entries do); an update's record
    // names it too. A title left by a cut after AM committed is then completed next time.
    // (install_start_plan copies record_title() into known_titles).
    j->nknown = k == S_CIA_EXTERNAL || k == S_CIA_UPDATE || k == S_CIA_FRESH ? 1 : 0;
    return j;
}

static void clear_job(Job *j) {
    json_decref(j->files); json_decref(j->dirs); json_decref(j->hashes);
    json_decref(j->record); json_decref(j->script);
    j->files = j->dirs = j->hashes = j->record = j->script = NULL;
}

// Runs one installation (or, with recover, only the recovery) in a child process;
// crash_at < 0 runs to the end. Returns the child's exit status.
static bool verbose;
static int run_child(Kind k, bool recover, long crash_at, long *mutations) {
    fflush(NULL);
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        host_am_file = true;
        host_auto_ack = true;
        host_mutations = 0;
        host_crash_at = crash_at;
        Job *j = recover ? NULL : start(k);
        if (recover) tx_recover();
        else if (j->uninstall) run_uninstall(j);
        else run(j);
        host_crash_at = -1;
        if (j && j->state != JOB_DONE && verbose) fprintf(stderr, "    [%s] job ended %d: %s\n", NAMES[k], (int)j->state, j->error);
        FILE *f = fopen("mutations.txt", "w");
        fprintf(f, "%ld\n", host_mutations);
        fclose(f);
        _exit(!j || j->state == JOB_DONE ? 0 : 1);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    if (mutations) {
        FILE *f = fopen("mutations.txt", "r");
        assert(f && fscanf(f, "%ld", mutations) == 1);
        fclose(f);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
static int child(Kind k, long crash_at, long *mutations) { return run_child(k, false, crash_at, mutations); }

static bool kept_aside(void) {
    DIR *d = opendir(DATA_DIR);
    bool kept = false;
    struct dirent *e;
    while (d && (e = readdir(d))) kept |= !strncmp(e->d_name, "install-transaction-kept-", 25);
    if (d) closedir(d);
    return kept;
}

static char *replace_record(const char *state, const char *record_line) {
    const char *r = strstr(state, "\nR ");
    assert(r);
    size_t head = (size_t)(r - state) + 1;
    char *out = malloc(head + strlen(record_line) + 2);
    memcpy(out, state, head);
    sprintf(out + head, "%s\n", record_line);
    return out;
}

typedef enum { RUN_NORMAL, RUN_COMMIT_FAILS, RUN_AM_FAILS } Mode;
static const char *MODES[] = {"", ", record not saved", ", AM refusing it"};
static void mode_flags(Mode m) {
    host_commit_ok = m != RUN_COMMIT_FAILS;
    host_am_fail_finish = m == RUN_AM_FAILS;
}

// `state` with its title line taken from `other`.
static char *with_title_of(const char *state, const char *other) {
    const char *t = strstr(state, "\nT "), *u = strstr(other, "\nT ");
    assert(t && u);
    size_t head = (size_t)(t - state) + 1, tl = strcspn(t + 1, "\n"), ul = strcspn(u + 1, "\n");
    char *out = malloc(strlen(state) + ul + 2);
    memcpy(out, state, head);
    memcpy(out + head, u + 1, ul);
    strcpy(out + head + ul, t + 1 + tl);
    return out;
}

// Every line of `now` (folders, files and their contents) was already there before.
static bool only_removed(const char *now, const char *before) {
    for (const char *l = now; *l;) {
        size_t n = strcspn(l, "\n");
        if (l[0] != 'T' && l[0] != 'R' && l[0] != '-') {
            bool found = false;
            for (const char *b = before; *b && !found;) {
                size_t m = strcspn(b, "\n");
                found = m == n && !strncmp(b, l, n);
                b += m + (b[m] == '\n');
            }
            if (!found) return false;
        }
        l += n + (l[n] == '\n');
    }
    return true;
}

// Exactly the previous version, exactly the new one or, for an update over a title installed
// without forge.store, the new one unrecorded. A failing run can never end committed.
// Since 1.4.2 a CIA installs while it downloads (FBI's way), before the data files take
// their places: a cut right after AM commits leaves the new title with the previous files
// and record, which the next installation completes. An uninstallation only ever removes.
static bool acceptable(Kind k, Mode m, const char *now, const char *before, const char *after, const char *unrecorded) {
    if (path_exists(TX_DIR) || kept_aside()) return false;
    if (!strcmp(now, before)) return true;
    if (k == S_UNINSTALL) return only_removed(now, before);
    bool cia = k == S_CIA_FRESH || k == S_CIA_UPDATE || k == S_CIA_EXTERNAL;
    if (cia && m != RUN_AM_FAILS && !strcmp(now, unrecorded)) return true;
    if ((k == S_CIA_FRESH || k == S_CIA_UPDATE || k == S_CIA_EXTERNAL) && m != RUN_AM_FAILS) {
        char *mixed = with_title_of(before, after);
        bool same = !strcmp(now, mixed);
        free(mixed);
        if (same) return true;
    }
    return m == RUN_NORMAL && !strcmp(now, after);
}

static int scenario(Kind k, Mode m) {
    fixture(k);
    char *before = snapshot_state("demo");
    long total = 0;
    assert(child(k, -1, NULL) == 0);
    char *after = snapshot_state("demo");
    assert(strcmp(before, after));
    // An update over an FBI install that AM completed may end unrecorded (see the top).
    char *before_record_line = strstr(before, "\nR ") + 1;
    char line[64];
    snprintf(line, sizeof line, "%.*s", (int)strcspn(before_record_line, "\n"), before_record_line);
    char *after_unrecorded = replace_record(after, line);
    fixture(k);
    mode_flags(m);
    int status = child(k, -1, &total);
    mode_flags(RUN_NORMAL);
    assert(status == (m == RUN_NORMAL ? 0 : 1));
    int olds = 0, news = 0, unrecorded = 0, recovery_cuts = 0;
    for (long n = 0; n < total; n++) {
        fixture(k);
        mode_flags(m);
        status = child(k, n, NULL);
        mode_flags(RUN_NORMAL);
        assert(status == 77);  // it really died at that mutation
        // A second cut at every mutation of the recovery itself, then a full recovery.
        long recovery = 0;
        if (path_exists(TX_DIR)) {
            char *cut = snapshot_state("demo");
            assert(run_child(k, true, -1, &recovery) == 0);
            for (long r = 0; r < recovery; r++) {
                fixture(k);
                mode_flags(m);
                assert(child(k, n, NULL) == 77);
                mode_flags(RUN_NORMAL);
                char *again = snapshot_state("demo");
                assert(!strcmp(again, cut));  // the same state as the first time
                free(again);
                assert(run_child(k, true, r, NULL) == 77);
                tx_recover();
                char *now = snapshot_state("demo");
                if (!acceptable(k, m, now, before, after, after_unrecorded)) {
                    fprintf(stderr, "%s%s: cut %ld, then cut %ld/%ld during recovery: inconsistent\n--- now\n%s--- before\n%s--- after\n%s",
                            NAMES[k], MODES[m], n, r, recovery, now, before, after);
                    return 1;
                }
                free(now);
                recovery_cuts++;
            }
            free(cut);
            fixture(k);
            mode_flags(m);
            assert(child(k, n, NULL) == 77);
            mode_flags(RUN_NORMAL);
        }
        tx_recover();
        char *now = snapshot_state("demo");
        if (!acceptable(k, m, now, before, after, after_unrecorded)) {
            fprintf(stderr, "%s%s: crash before mutation %ld/%ld left an inconsistent state\n--- now\n%s--- before\n%s--- after\n%s",
                    NAMES[k], MODES[m], n, total, now, before, after);
            return 1;
        }
        olds += !strcmp(now, before);
        news += !strcmp(now, after);
        unrecorded += k == S_CIA_EXTERNAL && !strcmp(now, after_unrecorded);
        // The next installation starts fresh and completes.
        assert(child(k, -1, NULL) == 0 || k == S_UNINSTALL);
        free(now);
    }
    char name[96];
    snprintf(name, sizeof name, "%s%s", NAMES[k], MODES[m]);
    printf("  %-48s %3ld cuts (+%4d during recovery): %2d previous, %2d new%s\n", name, total, recovery_cuts, olds, news,
           unrecorded ? ", some new unrecorded" : "");
    free(before); free(after); free(after_unrecorded);
    return 0;
}

// ---- failures the installer sees itself (no power loss) -------------------------------
static void in_session(void) {
    struct { Kind k; bool commit_fails, am_fails; int expect; } cases[] = {
        {S_CIA_FRESH, true, false, 0},     // record not saved: the new title is removed again
        {S_CIA_UPDATE, true, false, 2},    // record not saved: no package is kept since 1.4.2, kept complete
        {S_CIA_EXTERNAL, true, false, 2},  // record not saved, no previous package: kept complete
        {S_CIA_UPDATE, false, true, 0},    // AM refuses the package: nothing changed
        {S_CIA_EXTERNAL, false, true, 0},
        {S_3DSX_UPDATE, true, false, 0},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        Kind k = cases[i].k;
        fixture(k);
        char *before = snapshot_state("demo");
        assert(child(k, -1, NULL) == 0);
        char *after = snapshot_state("demo");
        char *line = strstr(before, "\nR ") + 1, record[64];
        snprintf(record, sizeof record, "%.*s", (int)strcspn(line, "\n"), line);
        char *unrecorded = replace_record(after, record);
        fixture(k);
        host_commit_ok = !cases[i].commit_fails;
        host_am_fail_finish = cases[i].am_fails;
        int status = child(k, -1, NULL);
        host_commit_ok = true;
        host_am_fail_finish = false;
        char *now = snapshot_state("demo");
        const char *want = cases[i].expect == 0 ? before : cases[i].expect == 1 ? after : unrecorded;
        if (status != 1 || strcmp(now, want) || path_exists(TX_DIR)) {
            fprintf(stderr, "%s: in-session failure %u ended wrong (status %d)\n--- now\n%s--- want\n%s", NAMES[k], i, status,
                    now, want);
            exit(1);
        }
        free(before); free(after); free(unrecorded); free(now);
    }
    puts("  failures without power loss: record not saved (x4 kinds), AM refusing the package (x2)");
}

// ---- a failed installation, then the same one again, with no restart ----------------
// The rollback itself is made to fail (the previous file cannot be moved back), so the
// staging folder stays behind. Installing again in the same process must settle it first.
static void retry_same_session(void) {
    // A CIA update is no longer rolled back once AM commits (no package is kept since 1.4.2).
    Kind kinds[] = {S_3DSX_UPDATE};
    for (unsigned i = 0; i < sizeof kinds / sizeof *kinds; i++) {
        Kind k = kinds[i];
        fixture(k);
        char *before = snapshot_state("demo");
        assert(child(k, -1, NULL) == 0);
        char *after = snapshot_state("demo");
        fixture(k);
        fflush(NULL);
        pid_t pid = fork();
        assert(pid >= 0);
        if (!pid) {
            host_am_file = true;
            host_auto_ack = true;
            host_commit_ok = false;
            host_fail_old_rename = true;  // the rollback cannot move the previous file back
            Job *j = start(k);
            run(j);
            bool failed = j->state == JOB_FAILED;
            bool left = path_exists(TX_DIR);
            clear_job(j);
            host_commit_ok = true;
            host_fail_old_rename = false;
            j = start(k);  // the user taps Install again: same app session
            run(j);
            _exit(failed && left && j->state == JOB_DONE ? 0 : 1);
        }
        int status = 0;
        assert(waitpid(pid, &status, 0) == pid);
        char *now = snapshot_state("demo");
        if (!WIFEXITED(status) || WEXITSTATUS(status) || strcmp(now, after) || path_exists(TX_DIR) || kept_aside()) {
            fprintf(stderr, "%s: install again after a failed rollback did not complete\n--- now\n%s--- want\n%s", NAMES[k], now, after);
            exit(1);
        }
        free(before); free(after); free(now);
    }
    puts("  failed install whose cleanup also failed, then install again without restarting: completes");
}

// ---- leftovers that are not from a power cut at a known point -------------------------
static void leftovers(void) {
    // 1.0 inventory (a plain array) after a cut between the backup and the new file.
    fixture(S_3DSX_UPDATE);
    mkdir(TX_DIR, 0777);
    rename("sdmc:/3ds/Demo/demo.3dsx", TX_DIR "/0000.old");
    put(TX_DIR "/0001.new", "new-data");
    put("sdmc:/3ds/Demo/demo.3dsx", "new-3dsx");
    json_t *legacy = json_pack("[{s:s,s:s,s:s,s:b},{s:s,s:s,s:s,s:b}]", "path", "sdmc:/3ds/Demo/demo.3dsx", "stage",
                               TX_DIR "/0000.new", "backup", TX_DIR "/0000.old", "deleted", 0, "path",
                               "sdmc:/3ds/Demo/data.bin", "stage", TX_DIR "/0001.new", "backup", TX_DIR "/0001.old",
                               "deleted", 0);
    assert(!json_dump_file(legacy, TX_JOURNAL, 0));
    json_decref(legacy);
    tx_recover();
    FILE *f = fopen("sdmc:/3ds/Demo/demo.3dsx", "rb");
    char b[32] = {0};
    assert(f && fread(b, 1, sizeof b, f) && !strcmp(b, "old-3dsx"));
    fclose(f);
    assert(!path_exists(TX_DIR));
    // Only staged downloads (cut during a download): removed, nothing else changes.
    fixture(S_3DSX_FRESH);
    char *before = snapshot_state("demo");
    mkdir(TX_DIR, 0777);
    put(TX_DIR "/0000.new", "partial");
    tx_recover();
    char *after = snapshot_state("demo");
    assert(!strcmp(before, after) && !path_exists(TX_DIR));
    free(before); free(after);
    // An unreadable inventory next to a backup cannot be settled: set aside, never deleted.
    fixture(S_3DSX_FRESH);
    mkdir(TX_DIR, 0777);
    put(TX_JOURNAL, "{\"format\":2,\"fil");
    put(TX_DIR "/0000.old", "someone's file");
    tx_recover();
    assert(!path_exists(TX_DIR) && path_exists(DATA_DIR "/install-transaction-kept-1/0000.old"));
    // An inventory pointing outside the staging folder is not acted on.
    fixture(S_3DSX_UPDATE);
    mkdir(TX_DIR, 0777);
    json_t *evil = json_pack("{s:i,s:s,s:s,s:s,s:[{s:s,s:s,s:s,s:b,s:b}]}", "format", 2, "kind", "install", "app", "demo",
                             "transaction", "x", "files", "path", "sdmc:/user-save.dat", "stage", "sdmc:/3ds/Demo/data.bin",
                             "backup", TX_DIR "/0000.old", "publish", 1, "remove", 0);
    assert(!json_dump_file(evil, TX_JOURNAL, 0));
    json_decref(evil);
    tx_recover();
    f = fopen("sdmc:/user-save.dat", "rb");
    memset(b, 0, sizeof b);
    assert(f && fread(b, 1, sizeof b, f) && !strcmp(b, "my-progress"));
    fclose(f);
    assert(path_exists("sdmc:/3ds/Demo/data.bin") && !path_exists(TX_DIR));
    // The next installation settles a leftover by itself before starting (tx_begin).
    fixture(S_3DSX_UPDATE);
    mkdir(TX_DIR, 0777);
    put(TX_DIR "/0007.new", "stale");
    assert(child(S_3DSX_UPDATE, -1, NULL) == 0 && !path_exists(TX_DIR));
    puts("  leftovers: 1.0 inventory, staged-only, unreadable inventory, foreign paths, next install");
}

int main(void) {
    int failed = 0;
    host_am_file = true;  // AM state lives on the fixture SD, shared with the crashing children
    puts("install recovery (production install.c, mocked AM, simulated power loss):");
    for (Kind k = 0; k < S_COUNT; k++) failed |= scenario(k, RUN_NORMAL);
    failed |= scenario(S_3DSX_UPDATE, RUN_COMMIT_FAILS) || scenario(S_CIA_FRESH, RUN_COMMIT_FAILS) ||
              scenario(S_CIA_UPDATE, RUN_COMMIT_FAILS) || scenario(S_CIA_EXTERNAL, RUN_COMMIT_FAILS) ||
              scenario(S_UNINSTALL, RUN_COMMIT_FAILS) || scenario(S_CIA_UPDATE, RUN_AM_FAILS) ||
              scenario(S_CIA_EXTERNAL, RUN_AM_FAILS);
    if (failed) return 1;
    in_session();
    retry_same_session();
    leftovers();
    if (failed) return 1;
    puts("PASS: every crash point ends at the previous or the new version (a CIA cut after AM: new title, previous files); the next install succeeds");
    return 0;
}
