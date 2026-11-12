# Catalog integration API

`catalog.c` and `catalog.h` own the installed JSON. All installed reads/mutations and settings save scheduling must run on the **main thread**. Save workers receive serialized settings snapshots only; they never access installed JSON.

```c
bool installed_commit(App *app, const char *version, bool experimental,
                      bool cia, json_t *files, json_t *dirs);
bool installed_commit_manifest(App *app, const char *version, bool experimental,
                               bool cia, json_t *files, json_t *dirs, json_t *hashes);
bool installed_forget(App *app);
json_t *installed_record(App *app);
bool app_requirement_met(App *app, json_t *requirement);
bool app_launch_path(App *app, char *out, size_t size);
```

## Installer acknowledgment

1. Worker finishes publishing staged files, retaining its file backups, and exposes immutable `files`/`dirs` arrays with the resolved release tag/channel/format.
2. `install_poll()` on the main thread calls `installed_commit_manifest()` while the worker waits for acknowledgment. Pass the **actual selected release channel**, not the user's preference; `app_release_experimental(app, release)` identifies it.
3. Propagate its boolean to the worker. `true` means the JSON was serialized, written, flushed with `fflush`/`fsync`, closed, reparsed and compared, then successfully promoted to `installed.json`. Only then drop file backups/report success. `false` means preserve/restore file backups and report durable-save failure. In-memory installed metadata remains unchanged.
4. Publish acknowledgment with the installer's synchronization protocol. The catalog does not supply cross-thread ownership or signaling.

`installed_commit_manifest()` also borrows an optional `hashes` object mapping inventory paths to SHA256 strings. It merges previous hashes then new hashes (new values win) in the same durable transaction. `installed_commit()` delegates with NULL hashes. Installer computes actual published-file hashes; catalog does not hash files. Inventory may include `title:<16hex>` markers; launch lookup ignores these.

Both arrays are **borrowed**, may be NULL, and are copied into merged metadata. Caller retains responsibility for decref. Do not pass an extra `json_incref()` unless that extra reference is subsequently released. Do not mutate arrays concurrently with the commit.

`installed_record()` returns a **borrowed, read-only** object or NULL. It becomes invalid on successful installed mutation or `installed_load()`; consume it during the main-thread frame or retain your own reference. Fields: latest `version`, `channel` (`stable`/`experimental`), `format` (`cia`/`3dsx`), UTC installation `date` (`YYYY-MM-DD`), compatibility flags `experimental`/`cia`, and ordered deduplicated unions `formats`, `files`, `dirs`. Legacy format flags migrate conservatively. Existing unknown fields, including any future `titleId`, survive updates; this module does not create CIA title IDs. An old `cia:true` is retained across 3DSX installs. Only explicitly supplied directories are owned; parent folders are not inferred.

`installed_set()` remains a legacy void wrapper with borrowed `files`, no new dirs, and discarded failure result. It is unsuitable for installer success acknowledgment. `installed_forget()` removes metadata only and publishes the in-memory removal only after synchronous save success; false leaves the in-memory record untouched. An already absent ID is an idempotent durable save. It does not uninstall titles/files. `installed_remove()` is the legacy void wrapper that discards its result.

## Recovery and filesystem limits

Settings and installed saves share the same serialized FAT-safe transaction. Validate `.tmp` before moving the previous valid primary to `.bak`; promote `.tmp` only into an absent primary name. On failed promotion, restore `.bak` where possible, otherwise retain it for loading. The loader prefers a valid JSON object in the primary, then `.bak`, then a preserved `.bad` primary; an unacknowledged `.tmp` is never used. An unreadable primary is renamed to `.bad` without overwriting `.bak`; it is removed only after successful promotion, and preserved on failed restoration. A backup may remain after success until the next save. Settings stay asynchronous; installed commits are synchronous and may stall a frame during SD I/O. `saves_flush()` drains pending writers rather than returning after a fixed timeout.

`fsync` is checked (the installed libctru archive driver routes it to `FSFILE_Flush`). This is application-level recovery, not a guarantee against arbitrary SD-controller/FAT corruption. JSON and installed files are not one cross-file power-loss transaction. The installer must own recovery of a crash between file publication, JSON promotion, acknowledgment, and deletion of backups. A power cut after JSON promotion can expose the new record even before acknowledgment reaches the worker.

## UI helpers

- `app_requirement_met()` performs a fresh filesystem check for a nonempty regular file. Folder requirements append `example` or `file.bin`. Pathless, invalid, traversal, truncated and non-SD paths remain unmet. No ROM hashes or `accept` wildcard alternatives are checked. `app_missing_requirement()` returns the first unmet requirement. `path_exists()` retains its general file/directory semantics.
- `app_launch_path()` searches recorded file strings for a case-insensitive `.3dsx` suffix, checks a nonempty regular file, and returns a normalized `sdmc:/...` path. It clears output on failure and rejects insufficient output buffers, traversal, other devices, and missing files. It never guesses from the catalog folder and never launches anything.
- `App.featured`, `App.has_stars`, and `App.old_experimental` supplement existing home metadata. `has_stars` distinguishes absent stars from zero; `old_experimental` distinguishes the Old 3DS warning from unsupported status. `app_release_experimental()` describes the actual selected channel. `app_download_size()` falls back to the available format and saturates at `UINT32_MAX`.
- `catalog_search()` normalizes query and joined fields (including localized tagline), ranks title prefix/substring before other fields, then date/ID. Empty normalized queries return at most five featured/forge released apps in original catalog order. Unicode lowercase/canonical decomposition data is embedded; Hangul decomposes algorithmically. No locale-dependent byte folding is used.
- Release/asset/step accessors reject malformed types and missing tags. Experimental needs a strictly newer valid date when stable exists. Update checks use the recorded channel and valid tag inequality, matching the web; they are not semantic-version upgrade ordering or remote catalog refresh.

## Validation

Run `python3 3ds/tests/catalog-host.py`. It compiles the actual production `catalog.c` and headers into a temporary host harness with graphics/platform stubs and real Jansson, using AddressSanitizer and UndefinedBehaviorSanitizer. It does not modify build outputs or package the app. ARM verification is separately limited to compilation of the owned `catalog.c` translation unit; no link, final build, emulator run, or hardware validation is implied.

Host result: 211 assertions passed with clang AddressSanitizer and UndefinedBehaviorSanitizer. Faults cover serialization, short write, fflush, fsync, fclose, readback mismatch, backup deletion, primary rotation, temporary promotion, and rollback rename; recovery and durable-forget failure are checked with real JSON files. SDK locks/threads/graphics are fixture stubs, so these tests do not validate actual concurrent scheduling or 3DS SD hardware. ARM `catalog.c` compiled separately with devkitARM GCC 16.1.0 and `-Wall -Wextra -Werror`; output `/tmp/catalog-owned.o`, without linking or packaging.

Owned ARM object SHA-256: `39097e96ec080eb3e2f3df05dd3a881a5156cc79edd14b275cb428c839255777`. The Unicode replacement table uses C hex byte escapes to preserve runtime UTF-8 without contributing thousands of glyphs to font-source scanning.
