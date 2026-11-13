# 3DS HOME banner engineering guide

This is the working procedure for future agents. Read it before changing a HOME
banner or distributing a banner-test CIA. A successful build, a Blender GIF and a
structural validator are different kinds of evidence. None certifies physical HOME.

## Incident and current status

On 2026-11-12, Forge's earlier banners orbited in physical HOME despite looking
correct in source previews. The user then reported that selecting test revision 13
froze the console. That is a **failed hardware result**, not “unverified success.”
Do not redistribute that test or reuse its QR as a recommendation.

The investigation found a definite packaging omission: Forge's test SMDH contained
flags `0x105`; Minish Cap's reference test contained `0x425`, including bit `0x20`.
Forge now explicitly requests `visible,allow3d,recordusage,extendedbanner` (`0x125`).
The validator reads the **icon inside the final CIA**, not merely the build command.
The SMDH bit is documented by [3dbrew](https://www.3dbrew.org/wiki/SMDH).
Its absence is confirmed. It has **not** been demonstrated to be the sole cause of
orbiting or freezing. Do not turn this correlation into a root-cause claim.

The local candidate also removes the extra pivot parent, supplies all nine rigid
TRS components explicitly, and retains only the varying Y rotation curve. This
reduces differences from the Minish reference; it is an experiment, not proven
hardware remediation. Its 518,080-byte CIA is distributed as an explicitly requested hardware test candidate, not a verified fix.

## Compare actual resources first

Inspect the file selected by each project's build command and then the resource
inside its CIA. A folder called `final` is not evidence that a build uses its file.
Keep original files and hashes. Do not overwrite other projects while comparing.

This investigation inspected 17 distinct local reference CGFX files plus two
historical Forge versions and the new candidate. Duplicate file contents were
collapsed. Detailed paths and binary reports remain private under `.local/`.

| Reference inspected | Bytes | Authored CANM | Relevant structure |
| --- | ---: | --- | --- |
| A Link to the Past, v4.2-e1 native source | 425,688 | None | Two model nodes; no billboard |
| The Minish Cap, native source | 298,072 | MinishHat | Rigid rotating hat; separate YAxial logo |
| Legend of Doom, native source | 436,344 | None | One rendered model node |
| Mario Kart 64, model asset | 304,824 | None | One rendered model node |
| Starfox, ready-to-use asset | 224,712 | ArwingFlight | Mixed model and billboard pieces |
| Starfox, sculpted-v2 output | 426,664 | ArwingFlight | Direct rendered pieces use YAxial |
| Worms, beta01 intermediate | 314,952 | Multiple rigid pieces | Billboard pieces and multiple motion tracks |
| Forge test 12 | 276,504 | Medallion | Empty facing parent plus rotating child |
| Forge test 13, user-reported freeze | 268,856 | Medallion | Translated empty parent; omitted position tracks |
| Forge local candidate | 263,432 | Medallion | Direct rigid node; explicit constant position and scale |

The user approves Starfox's appearance. There are two different Starfox assets;
confirm which is packaged before adopting either wholesale. Its sculpted-v2
`fixed_parts.py` deliberately removes rotational animation from the billboarded
Arwing and retains small translation offsets. Therefore it is **not** proof that
YAxial plus authored Euler rotation will reproduce Forge's desired coin wobble.

ALttP and Doom do not exercise authored CANM playback in these files. They are
useful packaging/model references, but not equivalent animation test cases.
Some archived Doom model assets fail dictionary lookup checks while the native
source resource passes. Do not replace the packaged file with an older asset just
because the filename looks newer or more final. Worms is an investigation reference,
not an assumed hardware-safe baseline: the user also reported problems there.

Read-only inventory tool:

```sh
python3 3ds/tools/audit-home-banner.py path/to/banner.cgfx
```

It reports model bones, parents, billboard modes, TRS and animation members. It is
an inventory, not a complete renderer or hardware validator.

## Authoring contract

Define desired screen-space behavior first: which parts stay fixed, which rotate,
the local pivot, angle range, period and expected silhouette. Forge's contract is
±24 degrees around its coin's own Y axis over 5.2 seconds, with welcome-screen
bezier timing. No vertical bob or orbit around the wordmark.

Keep the scene small and rigid. For this project use the pinned converter revision,
existing material setup, and existing C/libctru packaging. Do not upgrade tools to
solve a symptom without a controlled comparison. Do not add skinning to a rigid coin.

The [pycgfx source and README](https://github.com/skyfloogle/pycgfx) explain its glTF
conversion and the need to configure CGFX-specific billboarding separately. Our
wrapper regenerates the bone Patricia dictionary after the converter's alpha-order
sort and sets explicit mesh-to-bone names. Preserve these operations.

The [ClouDS Music tutorial and reference implementation](https://github.com/Epic0522/ClouDS-Music-FA)
are additional rigid-animation references. [The author's tutorial](https://gbatemp.net/threads/how-to-make-a-3d-banner-like-an-official-nintendo-3ds-game.683412/)
reports hardware freezes with soft skinning and recommends ordinary Transform (5)
node animation, resolved mesh names, and no joint/weight vertex attributes. It also
shows independently billboarded logos. This does not establish that Forge's freeze
was caused by skinning: Forge's primitive sets were already rigid.

For fixed billboard pieces, examine the reference's baked vertices and identity
node transforms together. Do not copy its billboard setting alone. An empty parent's
billboard is not evidence that a rendered child receives the same orientation.
Do not claim that moving translation to a parent mathematically guarantees the
appearance in HOME; revision 13 disproves that as a sufficient delivery criterion.

## Verification gates

1. **Source and conversion:** inspect the original source; require finite geometry,
   nonzero scale, correct normals/UVs, explicit names and the intended pivot.
   Reject skinning and joint/weight streams in the rigid workflow.
2. **Serialized CGFX:** verify every dictionary lookup, resolved mesh bindings,
   hierarchy references, animation target names, supported curve types and bounds.
   Check loop endpoints and sample the actual serialized transforms. Testing only
   the source glTF misses conversion bugs.
3. **Budgets:** respect the decompressed CGFX limit of `0x80000` bytes. Keep audio
   stereo and at most three seconds, as documented for [CBMD](https://www.3dbrew.org/wiki/CBMD).
   Preserve the tested formats and dimensions instead of treating any desktop
   texture as compatible. Forge uses RGB565, RGBA4 and LA8; its tests enforce these
   project choices, not a universal ban on every other PICA format.
4. **CIA:** extract ExeFS; verify file hashes, exact embedded BNR equality, correct
   title ID, and SMDH flags at `0x2028`. A `.3dsx` run cannot test the HOME banner.
5. **Preview:** source renders verify composition only. Label them as such. A render
   must not be described as the real HOME animation unless HOME produced it.
6. **Emulator HOME:** run the installed CIA through HOME, not just its application
   entry point. Record emulator version, profile and result. If HOME itself cannot
   boot, record that failure; launching the empty app is not a substitute.
7. **Physical HOME:** record console model, exact CIA SHA-256, repeated hover/select
   cycles, several loops, 3D slider states, sleep/resume and a video if available.
   A user-reported freeze rejects that build immediately regardless of host results.

Add regression tests for an observed omission. Do not write assertions that merely
enshrine a new hypothesis and then use their passing as proof of compatibility.
The current 13 host tests cover packaged resources, malformed inputs, explicit TRS,
constant pivot, SMDH flag omission and CIA integrity. They do not emulate HOME.

## Build and delivery procedure

```sh
3ds/tools/.venv/bin/python 3ds/tools/make-home-banner.py \
  --pycgfx 3ds/build/home-banner/pycgfx
# Review before copying the generated CGFX into 3ds/meta/.
python3 3ds/banner-test/build.py
FORGE_TEST_BNR=3ds/build/banner-test/banner.bnr \
FORGE_TEST_CIA=3ds/build/banner-test/forge-banner-test.cia \
python3 3ds/tests/test-home-banner.py
FORGE_DEBUG=0 zsh 3ds/build.sh
python3 3ds/tests/test-home-banner.py
```

Use the separate tiny test app for banner iterations. Do not make the user reinstall
49 MB of store resources to test a few hundred KB of banner. Keep its identity
separate from the store. Do not update the store's public binaries until the banner
has passed the required review. Do not manufacture a new identity every attempt.

When publication is authorized and appropriate, pin the QR to an immutable commit,
download that URL, compare its size and SHA-256 with the reviewed CIA, and decode
the QR back to that exact URL. Record the embedded banner hash and test result.
Never say “fixed” based only on successful upload or compilation.

HOME may cache banners, but do not blame caching whenever the user reports a failure.
First inspect the delivered bytes and package metadata. Do not suggest deleting
HOME extdata, themes, saves or icon arrangements as routine troubleshooting.

## Open investigation

The precise physical freeze trigger and the final screen-space wobble remain
unresolved. Missing `extendedbanner`, implicit TRS, parent hierarchy, and HOME's
own presentation transform must be distinguished experimentally. Preserve rejected
CIA hashes privately and change one factor per diagnostic comparison where possible.
No new QR should be represented as a confirmed repair without the corresponding
HOME evidence. Keep reporting compilation, host tests, emulator and physical results
separately, including failures.

## Local verification record for this candidate

- Native store build: passed with `FORGE_DEBUG=0 zsh 3ds/build.sh`; existing SDK
  `crtn.o` GNU-stack linker warning remains.
- Tiny test application: compiled and packaged, 518,080 bytes; CIA SHA-256
  `03ffe20ee6e3d0e6b099fdf9e2b437757915c7e4229c49276184109f8da33956`.
- Host checks: 13 passed against the tiny CIA and 13 against the locally built
  store CIA. These are the same suite applied to the two distinct packages.
- Repository publication consistency check: currently reports `Build/source
  mismatch: 3ds/build.sh`, because public `builds/3ds/` still contains the old
  published build. Do not rewrite its manifest to pretend it contains this candidate.
  Rebuild/package together only when publishing a reviewed store update.
- Emulator HOME: no successful validation in this investigation. The earlier HOME
  boot attempt failed; no new source render is being offered as a substitute.
- Physical HOME: revision 13 failed according to the user; current candidate untested.
- Publication: the user subsequently requested the tiny candidate CIA and its QR. Only the test binary is distributed; no store update, release or website deployment.
