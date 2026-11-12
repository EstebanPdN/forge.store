# Verification record

Publication candidate: **2026-11-11**, native C implementation plus web V2. This is a development
build, not a claim of complete parity or hardware qualification. Exact public binary hashes
and the source/media fingerprints are in [`builds/3ds/manifest.json`](../builds/3ds/manifest.json).

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

The public repository is `EstebanPdN/forje.store`; no release was created. All seven pinned
raw resource URLs were downloaded over anonymous HTTPS and matched the manifest's complete
SHA-256 hashes and sizes (16,044,949 bytes total). A clean `git archive` checkout also built
the native CIA and 3DSX using the included RomFS, without local intermediate assets.
GitHub's repository checks passed for the initial publication.

An isolated New 3DS profile running the public build started with **no video cache**.
All seven resources downloaded automatically on the first attempt, reached one complete
package (16,044,949 bytes), and matched their hashes on the emulator's SD. Scripted navigation
and shutdown completed in 68.7 seconds. Sampled store-music counters remained at zero
underruns; this is emulator evidence, not physical-console qualification.

The final packaged binary also completed an Old 3DS emulator run using those downloaded
files as its cache, with dark/light status captures and a visible trailer frame after a
seek to 30 seconds. Its executable hash matches the distributed 3DSX.
Machine-readable publication evidence is in [publication.json](verification/publication.json).
