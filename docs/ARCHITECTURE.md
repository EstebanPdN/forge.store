# Architecture

## Repository boundaries

| Path | Responsibility |
| --- | --- |
| `index.html`, `src/` | Maintained web V2 reference; plain ES modules and CSS |
| `assets/ports`, `assets/brand`, `assets/fonts`, `assets/audio` | Shared visual/audio sources |
| `assets/store-resources/` | Reviewed, compressed downloadable native videos and integrity manifests |
| `catalog/forge-store.json` | Curated application catalog, install plans, requirements and media metadata |
| `3ds/source/` | Native application in C; public interfaces in adjacent headers |
| `3ds/source/vendor/`, `3ds/vendor/` | Third-party code with original notices |
| `3ds/romfs/` | Versioned runtime assets: exact-size texture/font atlases, audio, strings and catalog snapshots |
| `3ds/meta/`, `3ds/forge-store.rsf` | HOME Menu art and unchanged application identity |
| `3ds/tools/`, `3ds/tests/` | Packaging, conversion, measurement and host/native test harnesses |
| `tools/` | Web server, catalog formatting and shared asset preparation |
| `builds/3ds/` | Reviewed distributable CIA and 3DSX, hashes and build record |
| `docs/`, `.github/` | Maintained documentation and repository checks |
| `versions/v1/` | Preserved original web design; not the maintained UI |

Build output goes to `3ds/build/` and is ignored. `.local/` holds private diagnostics and
historical material. Checked-in RomFS keeps an ordinary native build independent of the
other local projects and the one-time rendering environment.

## Native module map

- `main.c` initializes services, owns the frame loop, input, APT lifecycle, status bar and test commands.
- `scenes.c` owns navigation, shared tabs, introduction, Home and detail views. `panels.inc`
  holds Downloads, SD, Settings and the GitHub reader in that same translation unit.
- `gfx.c`, `fx.c` draw pre-rasterized fonts/images, geometric UI and banner effects.
- `audio.c` owns NDSP channels, streamed store music and UI sounds. Video uses channel 10.
- `media.c` owns the resource queue, manifest, cache verification, retries and cleanup.
- `video.c` owns the bounded frame/sound rings, FVD2/FVID decoder worker and video textures.
- `install.c`, `catalog.c`, `live_release.c`, `catalog_sync.c` handle install transactions,
  installed state, release metadata and curated refreshes.
- `universal_convert.c`, `universal_sync.c` convert and refresh Universal-DB.
- `store_update.c`, `store_update_policy.h`, `store_version.h` own the bounded startup check,
  build selection, verified self-update and restart request.
- `network.c`, `http_cache.c`, `reader_cache.c` fetch and persist bounded responses;
  `github_prefetch.c` validates common public pages/images while the store is idle.
- `gh_markup.c`, `gh_flow.c`, `gh_tables.c`, `gh_releases.c`, `remote_image.c` parse, lay out
  and render the bounded GitHub document/image subset.
- `file_view.c`, `sd_usage.c`, `rom_find.c` perform filesystem queries away from drawing.
- `hbl_launch.c`, `dsp_dump.c`, `debug_dump.c` isolate platform-specific handoff and diagnostics.

Headers document caller/thread contracts. Workers publish bounded snapshots; rendering does
not retain pointers to mutable worker-owned objects. Join timeouts are successful **only on
exactly zero**: the 3DS kernel can return a positive timeout code.

## Runtime flow

1. Load bundled settings/catalog/art and initialize services, graphics and audio.
2. Check for store updates with a five-second UI deadline. A verified newer build waits for
   confirmation, then downloads, installs and requests restart. Rejecting it fades to black
   and exits; a current/unavailable result fades into the introduction. The choice has no timeout.
3. Present the introduction/theme choice and bring Home into view. Start one persistent media
   worker and the low-priority GitHub prefetch pass once Home is usable. Keep pumping queues and input.
4. Verify each cached video or download to a private `.part` path, hash it and rename atomically.
5. Downloads aggregates all media items into one Store resources row. Each verified item can
   play independently while the rest continues.
6. Clear requests first stop the player; after the decoder releases its file, the media worker
   removes cache files. The same session stays cleared until an explicit retry or next launch.
7. Shutdown requests cancellation across modules before joining within a shared deadline.
   Modules retain resources still owned by unfinished workers; process termination reclaims them.

HOME/sleep handling is implemented, but the emulator cannot establish that physical HOME,
SD failures or suspend/resume behave correctly on every console. See Verification.
The update, cache and publication contracts are described in [Updates and cache](UPDATES-AND-CACHE.md).

## Web boundaries

`src/js/core/` owns input, state, i18n, catalog, simulated installation/SD, GitHub and audio.
`src/js/scenes/` owns screen behavior; `src/js/ui/` owns shared components. CSS tokens and
`native-ui.css` carry the low-resolution design adjustments. No framework migration or
bundler is necessary for this repository. Browser state lives in localStorage; console state
lives under `sdmc:/3ds/forge-store`.
