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
| Web preview | Loaded in Code Atlas | 23 curated apps, 400×240 + 320×240 screen boxes, no broken loaded images; requested status offsets checked |

The queue and video tests are different: video tests inject verified local files to isolate
playback correctness; they do not establish that a remote URL is available.

## Emulator evidence

Old and New 3DS profiles booted isolated copies of the current decoder build with the compressed
resource cache seeded explicitly. Both completed scripted navigation and closed normally.
Captured Home, Downloads package, Settings, SD, video and seek states. The package displayed
one complete resource row. The Old run played the ALttP trailer; the New run played the
ALttP dual-screen clip. Sampled store audio logs reported zero underruns in these runs.

Status-bar refinement was captured separately in dark and light themes with an explicit test
battery value of 82%: Wi-Fi position retained, battery moved up 1 px, time/percentage down
1 px from the previous baseline, percentage on the far right. Production reads real status;
the synthetic 82% value belongs only to this explicit test capture.

The screenshots use a debug build to drive test commands and capture framebuffers. The
published build disables private debug dumps. Source changes after a capture invalidate its
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

Remote publication verification is appended after the repository is created.
