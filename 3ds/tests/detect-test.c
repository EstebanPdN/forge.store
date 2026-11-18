// Recognizing apps installed without forge.store: the pure rules (detect_rules.h) and the
// production scan (detect.c) against a host "sdmc:" folder, mocked AM titles and a real
// Universal-Updater record file. Also runs the rules over the shipped catalogs.
#define FORGE_DETECT_HOST_TEST
#include "../source/detect.c"
#include <assert.h>
#include <sys/stat.h>

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static void put(const char *p, const char *s) {
    FILE *f = fopen(p, "wb");
    assert(f);
    fputs(s, f);
    fclose(f);
}
static void mkdirs(const char *p) {
    char b[512];
    snprintf(b, sizeof b, "%s", p);
    for (char *s = b + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(b, 0777); *s = '/'; }
    mkdir(b, 0777);
}

static void rules(void) {
    // Unique IDs as Universal-Updater turns them into titles; the template ID never counts.
    CHECK(detect_title_of(1009012) == 0x000400000F657400ULL);
    CHECK(!detect_title_of(0xFF3FF) && !detect_title_of(0) && !detect_title_of(-5) && !detect_title_of(0x100000));
    json_t *app = json_loads("{\"detect\":{\"ids\":[1009012,1009012,1045503,true,\"7\"]}}", 0, NULL);
    u64 titles[4];
    CHECK(detect_titles(app, titles, 4) == 1 && titles[0] == 0x000400000F657400ULL);
    json_decref(app);

    // Variables expand where forge.store and Universal-Updater's defaults put files.
    char out[DETECT_FILES][DETECT_PATH];
    int n = 0;
    detect_paths("%3DSX%/netpass.3dsx", out, &n, DETECT_FILES);
    CHECK(n == 2 && !strcmp(out[0], "sdmc:/3ds/netpass.3dsx") && !strcmp(out[1], "sdmc:/3ds/netpass/netpass.3dsx"));
    detect_paths("sdmc:/3ds/NETPASS.3dsx", out, &n, DETECT_FILES);  // FAT: same file
    CHECK(n == 2);
    n = 0;
    detect_paths("%NDS%/ftpd.nds", out, &n, DETECT_FILES);
    detect_paths("%FIRM%/GodMode9.firm", out, &n, DETECT_FILES);
    detect_paths("%3DSX%/Zelda 3DS/zelda3.3dsx", out, &n, DETECT_FILES);  // in a folder: no extra variant
    detect_paths("%ARCHIVE_DEFAULT%/x.zip", out, &n, DETECT_FILES);
    CHECK(n == 5 && !strcmp(out[0], "sdmc:/nds/ftpd.nds") && !strcmp(out[1], "sdmc:/ftpd.nds") &&
          !strcmp(out[2], "sdmc:/luma/payloads/GodMode9.firm") && !strcmp(out[3], "sdmc:/3ds/Zelda 3DS/zelda3.3dsx") &&
          !strcmp(out[4], "sdmc:/x.zip"));
    n = 0;
    detect_paths("%TMP%/a.3dsx", out, &n, DETECT_FILES);
    detect_paths("%UNKNOWN%/a.3dsx", out, &n, DETECT_FILES);
    detect_paths("/3ds/../../x.3dsx", out, &n, DETECT_FILES);
    detect_paths("relative.3dsx", out, &n, DETECT_FILES);
    detect_paths("/3ds/bad:name.3dsx", out, &n, DETECT_FILES);
    detect_paths(NULL, out, &n, DETECT_FILES);
    CHECK(n == 0);
    n = 0;
    for (int i = 0; i < 20; i++) {
        char p[32];
        snprintf(p, sizeof p, "/3ds/f%d.3dsx", i);
        detect_paths(p, out, &n, 3);
    }
    CHECK(n == 3);

    // Candidates: the entry's files first, then every .3dsx its own 3DSX plan writes.
    app = json_loads("{\"detect\":{\"files\":[\"%3DSX%/netpass.3dsx\"]},\"install\":{\"3dsx\":["
                     "{\"type\":\"downloadUrl\",\"output\":\"%TMP%/x.zip\"},"
                     "{\"type\":\"extractFile\",\"output\":\"/3ds/Demo/demo.3dsx\"},"
                     "{\"type\":\"downloadRelease\",\"output\":\"%3DSX%/netpass.3dsx\"},"
                     "{\"type\":\"move\",\"new\":\"/3ds/Moved.3DSX\"},"
                     "{\"type\":\"downloadRelease\",\"output\":\"/3ds/readme.txt\"}]}}", 0, NULL);
    n = detect_candidates(app, out, DETECT_FILES);
    CHECK(n == 4 && !strcmp(out[2], "sdmc:/3ds/Demo/demo.3dsx") && !strcmp(out[3], "sdmc:/3ds/Moved.3DSX"));
    json_decref(app);

    // Universal-Updater stamps, ISO times and plain dates.
    char stamp[DETECT_STAMP];
    CHECK(detect_stamp("2026-11-08 at 13:42 (UTC)", stamp) && !strcmp(stamp, "2026-11-08T13:42"));
    CHECK(detect_stamp("2026-11-08T13:42:24.847Z", stamp) && !strcmp(stamp, "2026-11-08T13:42"));
    CHECK(detect_stamp("2026-11-08", stamp) && !strcmp(stamp, "2026-11-08"));
    CHECK(!detect_stamp("---", stamp) && !detect_stamp("", stamp) && !detect_stamp(NULL, stamp) && !detect_stamp("2026/11/08", stamp));
    CHECK(detect_newer("2026-11-08T13:42", "2026-11-08T10:00") && !detect_newer("2026-11-08T10:00", "2026-11-08T10:00"));
    CHECK(!detect_newer("2026-11-08", "2026-11-08T10:00") && detect_newer("2026-11-09", "2026-11-08T23:59"));
    CHECK(!detect_newer("2026-11-07T23:59", "2026-11-08") && !detect_newer("", "2026-11-08") && !detect_newer("2026-11-08", ""));

    // Outdated: Universal-Updater installed an older entry, or the CIA version is lower.
    app = json_loads("{\"source\":\"universal\",\"title\":\"NetPass\",\"updatedAt\":\"2026-11-08T13:42\","
                     "\"releases\":{\"stable\":{\"date\":\"2026-11-08\",\"ciaVersion\":2048}}}", 0, NULL);
    CHECK(!strcmp(detect_uu_name(app), "NetPass"));
    CHECK(detect_outdated(app, "2026-10-01T09:00", 0, 0) && !detect_outdated(app, "2026-11-08T13:42", 0, 0));
    CHECK(detect_outdated(app, "", 0x000400000F657400ULL, 1024) && !detect_outdated(app, "", 0x000400000F657400ULL, 2048));
    CHECK(!detect_outdated(app, "", 0, 0));  // a .3dsx found by hand: nothing to compare
    json_decref(app);
    app = json_loads("{\"title\":\"Checkpoint\",\"releases\":{\"stable\":{\"date\":\"2026-10-13\"}}}", 0, NULL);
    CHECK(!detect_uu_name(app));  // a curated app is matched only through its "detect.uu"
    CHECK(!detect_outdated(app, "2026-10-13T08:00", 0, 0) && detect_outdated(app, "2026-10-12T08:00", 0, 0));
    CHECK(!detect_outdated(app, "", 0x000400000BCFFF00ULL, 1));  // no CIA version known: not claimed
    json_decref(app);
}

static App *add(const char *id, const char *json) {
    App *a = &APPS[NAPPS++];
    memset(a, 0, sizeof *a);
    a->id = id;
    a->j = json_loads(json, 0, NULL);
    assert(a->j);
    return a;
}

static void scan(void) {
    // A console where NetPass came from Universal-Updater (3DSX, older entry), Checkpoint was
    // installed with FBI (CIA, current version), mGBA was copied in a folder by hand, and
    // ftpd is not installed (only its Universal-Updater record remains).
    mkdirs("sdmc:/3ds/Universal-Updater");
    mkdirs("sdmc:/3ds/mgba");
    put("sdmc:/3ds/netpass.3dsx", "3dsx");
    put("sdmc:/3ds/mgba/mgba.3dsx", "3dsx");
    put("sdmc:/3ds/empty.3dsx", "");
    put("sdmc:/3ds/Universal-Updater/MetaData.json",
        "{\"Universal-DB\":{\"NetPass\":{\"installed\":[\"netpass.3dsx\"],\"updated\":\"2026-10-01 at 09:00 (UTC)\"},"
        "\"ftpd\":{\"installed\":[\"ftpd.3dsx\"],\"updated\":\"2020-01-01 at 00:00 (UTC)\"}},"
        "\"Other store\":{\"NetPass\":{\"installed\":[\"x\"],\"updated\":\"1999-01-01 at 00:00 (UTC)\"}}}");
    host_titles[host_ntitles] = 0x000400000BCFFF00ULL;
    host_versions[host_ntitles++] = 5168;
    host_titles[host_ntitles] = 0x0004000012345600ULL;  // something unrelated
    host_versions[host_ntitles++] = 1;
    App *netpass = add("udb-netpass", "{\"source\":\"universal\",\"title\":\"NetPass\",\"updatedAt\":\"2026-11-08T13:42\","
                                      "\"detect\":{\"ids\":[1009012],\"files\":[\"%3DSX%/netpass.3dsx\"]},"
                                      "\"releases\":{\"stable\":{\"date\":\"2026-11-08\"}}}");
    App *checkpoint = add("checkpoint", "{\"title\":\"Checkpoint\",\"detect\":{\"ids\":[774143],\"files\":[\"%3DSX%/Checkpoint.3dsx\"],"
                                        "\"uu\":\"Checkpoint\"},\"releases\":{\"stable\":{\"date\":\"2026-10-13\",\"ciaVersion\":5168}}}");
    App *mgba = add("mgba", "{\"title\":\"mGBA\",\"detect\":{\"ids\":[6686]},\"install\":{\"3dsx\":["
                            "{\"type\":\"extractFile\",\"output\":\"%3DSX%/mgba.3dsx\"}]},\"releases\":{\"stable\":{\"date\":\"2026-01-01\"}}}");
    App *ftpd = add("ftpd", "{\"title\":\"ftpd\",\"detect\":{\"ids\":[782069],\"files\":[\"%3DSX%/ftpd.3dsx\"],\"uu\":\"ftpd\"}}");
    App *empty = add("empty", "{\"title\":\"Empty\",\"detect\":{\"files\":[\"%3DSX%/empty.3dsx\"]}}");
    App *dev = add("dev", "{\"title\":\"In development\",\"detect\":{\"files\":[\"%3DSX%/netpass.3dsx\"]}}");
    dev->dev = true;

    host_installing = true;  // never while installing: the installation changes it anyway
    CHECK(!detect_pump() && !items);
    host_installing = false;
    CHECK(detect_pump());
    CHECK(netpass->found_path && !strcmp(netpass->found_path, "sdmc:/3ds/netpass.3dsx") && !netpass->found_title);
    CHECK(!strcmp(netpass->uu_updated, "2026-10-01T09:00"));
    CHECK(detect_outdated(netpass->j, netpass->uu_updated, netpass->found_title, netpass->found_version));
    CHECK(checkpoint->found_title == 0x000400000BCFFF00ULL && checkpoint->found_version == 5168 && !checkpoint->uu_updated[0]);
    CHECK(!detect_outdated(checkpoint->j, checkpoint->uu_updated, checkpoint->found_title, checkpoint->found_version));
    // Universal-Updater's "in folder" option.
    CHECK(mgba->found_path && !strcmp(mgba->found_path, "sdmc:/3ds/mgba/mgba.3dsx"));
    // A record without the files is not an installation; neither is an empty file.
    CHECK(!ftpd->found_path && !ftpd->found_title && !ftpd->uu_updated[0]);
    CHECK(!empty->found_path);
    CHECK(!dev->found_path);  // in development: nothing to install or update yet
    CHECK(!strncmp(detect_status(), "titles=2 ", 9));

    // An uninstallation forgets at once; the next scan sees what is really there.
    detect_forget("checkpoint");
    CHECK(!checkpoint->found_title);
    host_ntitles = 0;
    remove("sdmc:/3ds/netpass.3dsx");
    CHECK(detect_pump());
    CHECK(!netpass->found_path && !netpass->uu_updated[0] && !checkpoint->found_title && mgba->found_path);
    // Nothing requested: no scan.
    CHECK(!detect_pump());
}

// The shipped catalogs produce sensible candidates (no crash, no unsafe path).
static void shipped(const char *catalog, const char *universal) {
    json_t *c = json_load_file(catalog, 0, NULL), *u = json_load_file(universal, 0, NULL);
    CHECK(c && u);
    int apps = 0, with = 0;
    json_t *lists[2] = {json_object_get(c, "apps"), json_object_get(u, "apps")};
    for (int l = 0; l < 2; l++) {
        size_t i;
        json_t *app;
        json_array_foreach(lists[l], i, app) {
            char out[DETECT_FILES][DETECT_PATH];
            u64 titles[DETECT_TITLES];
            int nf = detect_candidates(app, out, DETECT_FILES), nt = detect_titles(app, titles, DETECT_TITLES);
            for (int k = 0; k < nf; k++) CHECK(!strncmp(out[k], "sdmc:/", 6) && !strstr(out[k], "/../"));
            for (int k = 0; k < nt; k++) CHECK(titles[k] >> 32 == 0x00040000 && ((titles[k] >> 8) & 0xFFFFF) != 0xFF3FF);
            apps++;
            with += nf || nt;
            const char *id = json_string_value(json_object_get(app, "id"));
            if (id && !strcmp(id, "alttp")) {
                bool own = false;
                for (int k = 0; k < nf; k++) own |= !strcmp(out[k], "sdmc:/3ds/Zelda 3DS/zelda3-3ds.3dsx");
                CHECK(own && nt >= 1);
            }
            if (id && !strcmp(id, "udb-netpass")) CHECK(nf == 2 && nt == 1 && titles[0] == 0x000400000F657400ULL);
        }
    }
    printf("  shipped catalogs: %d apps, %d recognizable when installed without forge.store\n", apps, with);
    json_decref(c);
    json_decref(u);
}

int main(int argc, char **argv) {
    rules();
    scan();
    if (argc == 3) shipped(argv[1], argv[2]);
    printf("PASS: %d detection checks (rules, scan, Universal-Updater records)\n", checks);
    return 0;
}
