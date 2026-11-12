# Verification record

Publication candidate: **2026.11.12.5**, native C implementation plus web V2. This is a development
build, not a claim of complete parity or hardware qualification. Exact public binary hashes
and the source/media fingerprints are in [`builds/3ds/manifest.json`](../builds/3ds/manifest.json).

## Default music gain — 2026.11.12.5

The music gain doubles from Q8 410 to 820 on the console and from 0.85 to 1.7 on the web.
The native PCM test covers all 65,536 input values, stereo balance and every bundled sample.
The music's measured RMS rises to 1.950× (opening) and 1.947× (loop) after smooth peak
protection, with no saturated samples. Intro and effect gains remain unchanged.
Web offline controls pass mute, video duck/restore and master-volume checks.
The [audio evidence](verification/music-volume.json) records exact build hashes and the
emulator smoke test. Physical speaker output has not been measured.

## Home preload and offline reader — 2026.11.12.4

The [evidence index](verification/home-preload.json) records the exact public executable,
matching native-source fingerprints, selected measurements and capture hashes. Full local
logs, private SD fixtures and memory dumps are excluded.

- The CIA is 48,657,344 bytes, down from 54,399,936 (10.6%), while adding local reader
  content and backgrounds. The music predictor preserves every PCM sample; its two streams
  shrink from 14,760,960 to 6,989,447 bytes. The host harness verifies opening/loop seams,
  two loops, restart, CRC/truncation, malformed headers and missing files under ASan/UBSan.
- Production texture streaming passed bounds, malformed banks, priority promotion,
  cancellation before/after decode, reacquisition, queue overflow and shutdown under
  ASan/UBSan and TSan. FAT cache replacement and failed-promotion recovery also passed.
- Catalog tests passed 297 assertions; installer tests passed 27 scenario groups; the
  HTTP/updater suite passed 23 scenarios and concurrent cache access under TSan. All 148
  bundled responses and 209 prepared images passed their bounds, CRC and hash checks.
- Code Atlas opened all 144 repository endpoints and the profile from local data with
  API requests blocked. All 209 image URLs resolved to bundled web files. The support
  links use compact 18-pixel rows. JavaScript syntax and Python compilation passed.
- On the final Old 3DS emulator run, preloading finished in 953 ms. ALttP, Minish and
  Mutiny backgrounds/logos were already ready at the first sampled frame after hover;
  returning to ALttP retained one decode per layer. The forge.store reader completed
  its local data load in 173 ms, and the ALttP README sample had all five visible images resident.
- The same executable played the seeded ALttP trailer, retained its gallery on B, opened
  lower screenshots fullscreen for ALttP and Mutiny, and returned normally. The final New
  3DS run reviewed light/dark Settings, the Minish framing, Star Haven and Quake backgrounds,
  and the bundled store page. Both runs exited normally with zero sampled music underruns.
- Forge Mode retained its 100,000-round derivation and existing encrypted format. Independent
  host vectors and authenticated-envelope checks passed. The synthetic invalid-code derivation
  took 5,216 ms in the Old emulator; it is improved but is not instantaneous.

These are emulator observations, not physical-console latency or self-update qualification.
Capturing frames also introduces stalls, so no zero-jank claim is made. Physical HOME/suspend,
SD-card speed and real CIA self-update remain unqualified. Earlier build records follow.

## Catalog, cached content and artwork — 2026.11.12.3

All three final emulator runs use the exact packaged public 3DSX and matching native sources.
The [evidence index](verification/catalog-loading.json) records binary and capture hashes.
Private SD fixtures, memory dumps and full logs are excluded from publication.

- Production catalog tests passed 297 assertions under ASan/UBSan, including the bounded
  three-pair gallery, sparse lower screens and texture retention across video return.
- Installer integrity passed 27 scenario groups. Forge Mode authentication, tampering,
  persistent unlock and worker handoff passed; AM/CIA services remain mocked on the host.
- HTTP tests passed 23 updater scenarios, concurrent cache use under TSan, offline bundle
  fallback and all 152 bundled response keys, sizes, CRCs and hashes. The prepared-image
  cache passed invalidation, corruption, bounds and all 225 bundled image checks.
- The hammer/update icon harness passed 3,314 native pixel masks and 49 transitions.
  Its hammer head stays perpendicular to the handle through the strike.
- Code Atlas reviewed all 39 banner compositions with no missing images. It verified
  the 40 px About coin without its contact shadow, 10 px text gap, 18 px Forge mark,
  moving thumbnail cursor and 320×240 lower fullscreen with B returning normally.
- Old 3DS played the actual cached ALttP trailer. The three gallery slots were already
  ready two sampled frames after B, and stayed ready when returning from GitHub.
  These are residency observations, not physical-SD latency measurements.
- Old/New emulator runs reviewed banners, origins, unknown progress, both Settings themes,
  Forge Mode, README/profile data and Mutiny lower screenshots. GitHub started without
  an HTTP cache seed; the Old run showed 218 README nodes by its 30-frame sample.
  All three runs closed normally and sampled music logs reported zero underruns.

The visual Old/New runs explicitly seed no network-response cache. Two Old runs seed the
verified video package to isolate playback and return behavior. The New run starts without
that video seed. No physical-console self-update installation or HOME/suspend qualification
is claimed. The sections below retain results from earlier published builds.

## Native audio, gallery motion and update confirmation

The new startup flow checks on every launch, shows the offered version before downloading,
and waits for Yes/No. No exits with a black fade. Its icon family uses native integer pixels:
spinner → download arrow → check, or spinner → check when current. Both confirmation buttons
are 88 pixels wide. The gallery now pushes both images over 340 ms after their textures load.
The video mute button uses the same complete icon as web V2; no separate diagonal is positioned
over it. One continuous orange frame now slides between thumbnails with the tabs' sampled
340 ms easing, including reversal during motion. Home's approved 90% opacity is unchanged.

- Twenty-three production-source updater scenarios passed ASan/UBSan. Rejection and shutdown
  while waiting issue no binary request and create no staging file. Acceptance, rejection and
  waiting-worker shutdown also passed TSan. AM installation services remain mocked.
- All 19 bundled PCM files passed the production gain routine without saturated samples:
  music +4.09 dB, intro +3.52 dB and effects +9.54 dB. The host test also covers all 65,536 signed
  input values. These gains precede the existing DSP mixer ceiling.
- The native icon harness passed 1,630 binary coverage masks and all 25 state transitions,
  including interrupted transitions, three-pixel stroke geometry and actual downloading motion.
  One emulator download-icon capture matched all 1,600 pixels of its 40×40 host mask at 1:1.
- The regenerated font assets passed 48,675 glyph checks. Text tests passed 1,343,072 pair/phase
  cases, 1,379,636 strings and 235,284 wrapped lines, including the new localized prompts.
- Code Atlas verified four simultaneous 340 ms web animations: outgoing/incoming images on
  both screens. At midpoint they moved 200 pixels above and 74 pixels below. Repeated navigation
  left one current lower layer and no failed loaded gallery images.
- The shared native tabs sampler passed 762 geometry/timing checks. Web thumbnail cursor and
  track use the same `cubic-bezier(.22,1,.36,1)` curve over 340 ms; the cursor is 50×32 pixels.

Emulator update offers, download progress and installation-complete screens use explicit UI
fixtures; those fixtures never install a title. Actual newer-build installation and restart
still need physical-console validation. Captures and recording summaries are indexed in
[update-confirmation.json](verification/update-confirmation.json).

## Earlier gallery, startup and cache refinement

The preceding build introduced gallery corrections, 90% Home/status opacity, Settings gear,
increased audio mix, startup updates and shared GitHub cache/prefetch. Its evidence
is recorded separately in [refinement.json](verification/refinement.json).

- The public CIA and 3DSX compile with devkitARM GCC 16.1.0. The existing SDK `crtn.o`
  GNU-stack linker warning remains; no new source compilation warning was reported.
- Twenty production-source updater scenarios passed ASan/UBSan, including malformed metadata,
  deadlines, invalid size/hash, incomplete downloads, wrong title IDs, AM short writes/finalize
  failures and valid CIA/3DSX paths. AM operations are mocked; this is not physical self-update evidence.
- HTTP cache tests passed reset retry, zero-network reuse, conditional 304, changed-body and
  offline fallback cases. Four concurrent readers/writers made 24 requests under TSan without
  a reported race. Freshness timestamps, not SD filesystem modification times, drive expiry.
- Regenerated fonts passed all 48,675 glyph/pixel checks. The production text harness passed
  1,343,072 pair/phase cases, 1,379,540 strings and 234,836 wrapped lines under ASan/UBSan.
- The unchanged installer transaction harness passed 22 scenario groups; GitHub markup/flow,
  table/release layout and raster decoder checks also passed during this refinement.
- An isolated Old 3DS profile ran the packaged public executable through startup, Home in
  both themes, trailer controls, gallery transitions, Castle of Hyrule, GitHub profile/README,
  return from Home with cached badges, and dual-screen playback. Fifteen captures retain the
  exact executable/source hashes. The script completed normally; sampled music counters
  remained at zero underruns. Videos were explicitly seeded with the seven verified resources.
- A New 3DS profile then ran the same published executable with empty video and HTTP caches.
  The live startup check reached `UPDATE_CURRENT`. All seven resources downloaded on their
  first attempt and matched their full hashes; Downloads showed one complete package. The
  trailer played with audio, the script exited normally, and sampled music underruns remained
  zero. Ten captures preserve the final executable hash. This confirms checking and downloading,
  not installation of a newer store build on physical hardware.
- Code Atlas checked the web gallery at native screen size. Unaligned, unmasked comparisons
  of the whole lower screen measured 84.23% of trailer pixels and 79.33% of Castle pixels within
  RGB tolerance 16/255. Exact matches were 18.87% and 17.93%; these are two scenarios, not a
  global parity score. Text rasterization, strip layout and live download-size metadata differ.
- All seven resource URLs under the corrected repository name were downloaded anonymously
  and matched their complete SHA-256 hashes and combined 16,044,949-byte size.
- Both published binaries were downloaded anonymously and matched the packaged sizes/hashes.
  Repository CI passed for code commit `261ddb5`. The delivered QR was decoded independently
  and matched the CIA URL pinned to that commit. No GitHub release was created.

The startup rendering regression was reproduced with the new startup-only drawing path and
disappeared after reusing the shared scene background renderer. Final Home captures show the
correct text colours, rounded corners and badges while retaining 90% opacity. The precise
low-level GPU cause is not established; the evidence supports the shared-renderer correction.

## Automated checks run for this candidate

| Area | Result | Scope |
| --- | --- | --- |
| Native build | CIA + 3DSX linked | devkitARM GCC 16.1.0, public debug-disabled configuration |
| Video lifecycle | 2,023 checks, ASan/UBSan and TSan | Production decoder, malformed headers/records, async stop, stalled worker, seeks, overload, legacy compatibility |
| Full compressed set | All 7 files, 6,010 frame records | Every decoded picture compared with FFmpeg reference; both trailer sound streams compared with reference PCM |
| Media validation | 7 files, 16,044,949 bytes | Header/index/record boundaries, keyframe flags, ADPCM state, full Theora decode, sizes and hashes |
| Background queue | 14 scenario groups, ASan/UBSan | Cache reuse/corruption, priority, retries, permanent errors, pause, clear, cancellation, unsafe paths |
| Installer integrity | 22 scenario groups, ASan/UBSan | Real staging/rollback code; AM/CIA service behavior mocked |
| Catalog | 242 assertions, ASan/UBSan | Persistence failures, requirements, search, channels, Universal registration and art working set |
| SD file view | 20,061 assertions per sanitizer | Real temporary filesystem, refresh/TTL, stale handles, OOM/thread retry, shutdown; ASan/UBSan + TSan |
| Shared tabs | 762 checks | Exact geometry, 340 ms endpoints, interruption continuity and cursor bounds |
| Font/text | Passed | FNT3 metrics, replacement/wrap, invalid UTF-8, large offsets, XY translation, missing atlas |
| GitHub markup | Passed | Mixed inline flow, UTF-8, lists/tables, release cards, bounded mutation/truncation |
| Web preview | Loaded in Code Atlas | 23 curated apps, 400×240 + 320×240 screen boxes, no broken loaded images; status cap boxes and battery centred |

The queue and video tests are different: video tests inject verified local files to isolate
playback correctness; they do not establish that a remote URL is available.

## Emulator evidence

Old and New 3DS profiles booted isolated copies of the current decoder build with the compressed
resource cache seeded explicitly. Both completed scripted navigation and closed normally.
Captured Home, Downloads package, Settings, SD, video and seek states. The package displayed
one complete resource row. The Old run played the ALttP trailer; the New run played the
ALttP dual-screen clip. Sampled store audio logs reported zero underruns in these runs.

Status-bar refinement uses the cropped FNT3 glyph bounds: 9 px numeral/percent ink inside a
23 px right capsule leaves 7 px above and below. The 11 px rounded battery outline leaves
6 px above and below, including a vertically centred terminal. Wi-Fi retains its position.
The battery's final optical position is 1 px right of equal horizontal layout gaps,
as requested after visual review, without moving the time or percentage.
The web uses trimmed capital boxes and equal battery gaps.
Dark/light captures include explicit 82%, full/charging and low-battery fixtures. Production
reads real status; synthetic battery values require an explicit local test script.

The earlier playback screenshots use a debug build. Final status/download captures use the
public build, which permits explicit test scripts but disables private memory dumps.
Source changes after a capture invalidate its
claim to represent a later binary; `azahar_run.py` records source and executable hashes.

## Corrections found during publication review

- Compressed resources existed but were not integrated into the production decoder/manifest.
- Runtime joins in video/media/file-view/Universal could accept a positive timeout as success.
  Only an exact zero now permits release; GitHub reader completion also avoids a blocking join.
- Video final cleanup now follows the app's shared shutdown deadline.
- Catalog host test omitted the new real ROM-detection implementation; the harness now links it.
- Build caching did not distinguish public/debug flags; changed flags now invalidate objects.
- A dependency symlink pointed outside the repository; it is local-only and excluded from Git.
- The old README described the native app as unimplemented and instructed a destructive full
  asset repack for normal builds. Current instructions use the reviewed included RomFS.

## Remaining qualification

Physical Old/New Nintendo 3DS testing is still required for HOME close/resume, sleep, sustained
video/music, low-memory situations and real CIA install/launch/uninstall. Emulator timings
are not console timings. The video decoder may skip pictures when overloaded to preserve a
responsive clock/audio path. It is not a hardware full-frame-rate guarantee.

The GitHub reader implements a bounded subset, not a browser. Complex documents, shaping/bidi,
remote glyph coverage and some effects still differ from the web. No global “100% identical”
claim is made. Interrupted install recovery and unsupported external CIA replacements retain
explicit limitations; they are not silently treated as successful.

## Publication verification

The public repository is `EstebanPdN/forge.store`; no release was created. All seven pinned
raw resource URLs were downloaded over anonymous HTTPS and matched the manifest's complete
SHA-256 hashes and sizes (16,044,949 bytes total). A clean `git archive` checkout also built
the native CIA and 3DSX using the included RomFS, without local intermediate assets.
GitHub's repository checks passed for the initial publication.

An isolated New 3DS profile running the public build started with **no video cache**.
All seven resources downloaded automatically on the first attempt, reached one complete
package (16,044,949 bytes), and matched their hashes on the emulator's SD. Scripted navigation
and shutdown completed in 68.7 seconds. Sampled store-music counters remained at zero
underruns; this is emulator evidence, not physical-console qualification.

The status-bar build also completed an Old 3DS emulator run using those downloaded
files as its cache, with dark/light status captures and a visible trailer frame after a
seek to 30 seconds. Its executable hash is preserved with that run's evidence.
Machine-readable publication evidence is in [publication.json](verification/publication.json).

## Typography refinement

The shared cursor now guarantees a minimum pixel gap between visible glyph bounds, using
the baked 32/255 coverage insets without changing the glyph textures. Word gaps, integer
monospaced cells, measurement, wrapping and ellipses use the same placement rules. The right
status numerals retain their separately approved metrics. DM Sans regular is shared by
“store” and the section name; missing brand glyphs use the existing UI font fallback.

The production-source ASan/UBSan harness passed **1,343,072 ASCII pair/phase cases** across
all 76 font styles, and checked **9,042 unique locale/catalog strings** in four UI roles,
including 233,718 wrapped lines, bounded ellipses and field labels. All 48,675 packed glyph
records/insets and their atlas pixels passed validation. Shared tab geometry passed 762 checks.

Visual review covered 30 emulator captures: the main panels, installation/error states,
GitHub views, both themes and Home in all eight languages. Follow-up fixes contain long
console field labels, keep the Portuguese language hint separate, fit the detail tabs and
give translated Cancel actions enough space; touch regions use the same updated bounds.
The final focused capture and binary hashes are recorded in
[typography.json](verification/typography.json). Physical-console typography review remains pending.
