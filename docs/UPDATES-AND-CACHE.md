# Store updates and GitHub cache

## Startup updates

The native store checks for its own updates on every launch, before the usual introduction.
This check is always enabled. It is separate from game releases and the Store resources package.

`store_update.c` fetches the compact `builds/3ds/update.json` manifest over verified HTTPS from
`EstebanPdN/forge.store`. The manifest and binaries are ordinary repository files, not GitHub
releases. A confirmed current build shows a success message; an unavailable or invalid response
continues to the introduction without claiming that the installation is up to date.

- Metadata is limited to 16 KiB. The network request has a 2.5-second connection timeout and
  4.5-second total timeout. A separate main-thread deadline ends the check after five seconds,
  even if the platform resolver has not returned. A late response cannot start an update.
- The manifest must contain a positive integer `store_build`, a bounded file size and a
  lowercase SHA-256 digest. Download destinations and binary URLs are fixed in the application.
- Only a larger build number offers an update. The touch screen shows a bounded `version`
  label, falling back to the numeric build if the label is absent or invalid. No binary request,
  staging file or installation starts before **Yes**. **No** fades both screens to black and
  closes the store. This decision has no timeout; HOME/exit can cancel the waiting worker.
- After confirmation, the complete file is staged on SD, flushed,
  and checked against its declared size and hash before installation. Failed downloads leave
  the running application available; the next launch checks again.
- CIA updates require both the staged CIA and the running title to match the store's fixed
  title ID, `000400000F0F5E00`. Installation uses AM's overwrite transaction and finalizes only
  after every byte was written. It never deletes the installed title to make room.
- 3DSX updates require the actual `sdmc:/.../*.3dsx` launch path. The old executable is retained
  beside it as `*.forge-previous.3dsx`. A failed replacement attempts to restore it. FAT rename
  is not a power-loss-atomic overwrite; the backup can be opened from Homebrew Launcher if the
  replacement was interrupted. Restart uses the available HBL handoff and depends on the loader.

The startup screen uses the same background renderer, typography and sounds as the rest of
the interface. A single native pixel icon family rotates while checking, morphs into a check
when current or into a download arrow when an update is available. The arrow moves during
download/installation and becomes the same check when finished. Its three-pixel strokes use
binary coverage and integer LCD coordinates, without filtered textures. Compact confirmation
buttons share their drawing and touch bounds. The store logo appears only in the following
introduction. Download/install progress appears
before a black fade and restart. HOME/sleep
pauses worker callbacks; final shutdown cancels work and observes the shared join deadline.
Production-source host tests cover failure paths with AM services mocked. Actual self-update,
restart and interrupted-write recovery still require physical-console qualification.

## Publishing another build

1. Increase `STORE_BUILD` and set `STORE_VERSION` in `3ds/source/store_version.h`; never reuse or
   decrease a published build value.
2. Build with `FORGE_DEBUG=0 zsh 3ds/build.sh` and complete the applicable checks.
3. Run `python3 tools/package-build.py`. It copies CIA/3DSX and generates `SHA256SUMS`, the full
   source fingerprint manifest and the compact `update.json` from those exact files.
4. Publish all generated metadata/binary outputs together with the matching source. Repository checks
   reject disagreement between the compact update record and the full build manifest.
5. Verify the anonymous download URLs and the startup check against the published record.

Old builds that predate this updater need one manual CIA/3DSX installation to gain it.

## Persistent GitHub responses

`http_cache.c` stores bounded response bodies under `sdmc:/3ds/forge-store/http-v1/`, keyed by
SHA-256 of URL and Accept header. Bodies use the existing CRC-checked cache format. Metadata
retains ETag, Last-Modified and the last successful validation time; a shared lock protects
publication and invalidation across workers. Readers open complete files without waiting behind
cache pruning; FAT replacement preserves the prior file until promotion succeeds.

The CIA also bundles 148 public metadata, README, release and commit responses for
the curated repositories and profile. Foreground reads use CRC-verified SD data or
the bundled response immediately; a failed refresh falls back to the last usable copy.
The background pass revalidates metadata, while catalog synchronization owns release refreshes
and keeps pinned multi-file packages on their reviewed version.

Foreground pages use available local data immediately, regardless of age.
Background metadata freshness is six hours, commits one day, and READMEs/images seven days.
Bundled timestamps also count as fresh, avoiding a first-launch request burst. Background validation uses
`If-None-Match` or `If-Modified-Since`; a 304 updates freshness without replacing the body.
Only successful, bounded responses are published, and API JSON must parse. A failed image
decode invalidates that cached payload. A usable prior response remains available during
network errors.

Uncached foreground reader requests have a six-second deadline; the page has an independent
eight-second ceiling so a stuck resolver cannot leave its spinner running indefinitely.

Transient connection/reset/partial-transfer errors and HTTP 502/503/504 receive one bounded
retry. Each attempt has a four-second connection timeout and a twelve-second total timeout.
The cache is capped at 192 payload files; GitHub JSON is limited to 1 MiB and each image to
2 MiB. This is a file-count cap, not a small fixed SD byte budget.

## Prefetch and rendering priority

After the startup update gate, during the introduction, `github_prefetch.c` makes one background pass per launch over the public
profile and six rotating curated repositories, including community apps, emulators,
utilities and forge.store. The rotation cursor uses the same CRC-checked, FAT-safe save path
as cached responses. The bundled baseline covers all of them; Universal-DB stays separate. It validates metadata, READMEs, commit
summaries, avatars and every image emitted by the bounded README parser, for both themes. Repeated image URLs
are deduplicated using 32-byte hashes (up to 512 per launch), saving 496 KiB compared
with retaining full URL buffers. Background metadata freshness is six hours and
image freshness seven days. This pass allocates no GPU textures and never needs an account or token.

Prefetch yields while the GitHub reader, installer, video player or Forge Mode derivation is active. Foreground
GitHub metadata uses at most two concurrent request slots; visible image decoding remains
single-worker. The avatar and first visible document images are prioritized over a large image
farther down. Downloads from the resource package pause while GitHub is foreground.

Decoded images have a 1 MiB GPU budget. Leaving the reader frees large images and retains at
most 128 KiB of small avatars/badges to reduce placeholders on return. This cache does not turn
the reader into a browser: supported image formats and the bounded Markdown/HTML/SVG subset
still determine what can be displayed. GitHub rate limits and an interrupted connection can
still delay content; retries do not guarantee remote availability.

## Prepared art and warm galleries

The build includes 209 public README images and avatars, resized to at most 376×240 (avatars to 64×64) and stored as
CRC-checked, zlib-compressed RGBA (`RIM1`). Their original URLs and hashes are recorded in
`assets/reader-art/manifest.json`. Those exact bundled URLs need no network request or
PNG/WebP/SVG decode on the console. Other decoded images use an atomic SD cache capped at
128 files; a changed HTTP payload invalidates only its decoded copy. Bundled art changes
with a store update, while live badges and new URLs continue through the normal HTTP path.

Gallery thumbnails use independent 46×28 textures. Full-size pictures occupy individual
512×256 RGB565 sheets. At most three top/bottom pairs stay resident: selected, previous and
next (1.5 MiB combined). Video playback and the GitHub reader preserve that working set.
Returning with B therefore reuses ready textures; Home or a different game releases them.
Sparse lower-screen entries retain their original index, fixing missing Mutiny previews.

The Old 3DS emulator test observed ready gallery slots on the first sampled frame after B
from cached video, and the same slots after GitHub. This is a resource-lifetime observation,
not a measured latency guarantee for physical SD cards.


## Home working set and startup

After the update screen fades to black, the native loader warms the Forge banners before
starting the usual introduction. It continues when ready (minimum 0.5 seconds), with a
six-second ceiling. It never inserts six seconds of idle time on an already ready device.

`stage-romfs.py` combines the unchanged FTX payloads into one sequential `FBN1` bank
(less than 8 MiB) held in ordinary RAM. Runtime GPU residency uses an 8 MiB LRU budget,
protects the current/previous banner, and prefetches nearby entries. Returning to a resident
banner does not decode or read it again. Visible requests outrank speculative work; one
decoded sheet at a time waits for upload, with a 384 KiB per-frame upload budget.

Web V2 retains decoded local images in a bounded 128-entry working set, warms Forge before
the intro and nearby/category images before hover. It also ships prepared reader HTML and
images, so the local preview does not require GitHub to open a curated page.

## Lossless music and Forge Mode

The ordinary build packages the two music streams as `FPC2` inside gzip. A reversible
second-difference predictor runs independently on the left/right PCM16 channels; gzip handles
compression and CRC. The worker reconstructs the original 32 kHz samples before applying the
existing audio gain. It feeds the same read-ahead ring and DSP buffers. Effects remain PCM.
The production-source test compares every sample across opening/loop boundaries and restarts,
and checks truncated streams, CRC failures, empty/missing files and malformed headers.

Forge Mode reuses the SHA-256 states after the HMAC pads instead of hashing those pads for
every PBKDF2 round. Its 100,000-round envelope, encrypted data and existing persistent unlock
remain compatible. Background transfers yield during derivation. No code or decrypted link
is logged or included in the test evidence.

## Updating the bundled reader

Export the committed public snapshot with `python3 3ds/tools/reader-snapshot.py`.
Use `--refresh owner/repo` for explicitly selected curated repositories, review the local
snapshot, then run `pack-reader-data.py --snapshot <snapshot.json> --web` and
`pack-reader-art.py --readmes <readmes.json> --include-community --extra-urls <avatars.json>`.
The HTML authoring step uses the pinned `markdown-it-py` dependency; image packing needs
Pillow and Sharp. Neither is needed during an ordinary native build.

The packer and repository checks require metadata, README, releases and commits for every
curated public repository, including forge.store. This prevents a future catalog addition
from silently losing its offline page. A repository without a public URL cannot have a
bundled GitHub page. The prepared profile uses a native layout of my public terminal card,
with live repository/follower counts and compact support links.
