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
publication, invalidation and reads across workers.

The CIA also bundles 152 public metadata, README, release and commit responses for
the curated repositories and profile. Foreground reads use CRC-verified SD data or
the bundled response immediately; a failed refresh falls back to the last usable copy.
The background pass revalidates metadata, while catalog synchronization owns release refreshes
and keeps pinned multi-file packages on their reviewed version.

Fresh foreground requests use disk immediately. The normal GitHub response TTL is one hour;
catalog callers can request six hours. Image freshness is one day. Background validation uses
`If-None-Match` or `If-Modified-Since`; a 304 updates freshness without replacing the body.
Only successful, bounded responses are published, and API JSON must parse. A failed image
decode invalidates that cached payload. A usable prior response remains available during
network errors.

Transient connection/reset/partial-transfer errors and HTTP 502/503/504 receive one bounded
retry. Each attempt has a four-second connection timeout and a twelve-second total timeout.
The cache is capped at 192 payload files; GitHub JSON is limited to 1 MiB and each image to
2 MiB. This is a file-count cap, not a small fixed SD byte budget.

## Prefetch and rendering priority

After the startup update gate, during the introduction, `github_prefetch.c` makes one background pass per launch over the public
profile and up to 24 curated Forge repositories. It validates metadata, READMEs, commit
summaries, avatars and every image emitted by the bounded README parser, for both themes. Repeated image URLs
are deduplicated using 32-byte hashes (up to 512 per launch), saving 496 KiB compared
with retaining full URL buffers. Background metadata freshness is six hours and
image freshness seven days. This pass allocates no GPU textures and never needs an account or token.

Prefetch yields while the GitHub reader, installer or video player is active. Foreground
GitHub metadata uses at most two concurrent request slots; visible image decoding remains
single-worker. The avatar and first visible document images are prioritized over a large image
farther down. Downloads from the resource package pause while GitHub is foreground.

Decoded images have a 1 MiB GPU budget. Leaving the reader frees large images and retains at
most 128 KiB of small avatars/badges to reduce placeholders on return. This cache does not turn
the reader into a browser: supported image formats and the bounded Markdown/HTML/SVG subset
still determine what can be displayed. GitHub rate limits and an interrupted connection can
still delay content; retries do not guarantee remote availability.

## Prepared art and warm galleries

The build includes 225 public README images and avatars, resized to at most 376×240 (avatars to 64×64) and stored as
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
