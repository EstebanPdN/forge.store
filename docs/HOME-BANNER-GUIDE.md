# 3DS HOME banner engineering guide

This is the working procedure for future agents. Read it before changing a HOME
banner or distributing a banner-test CIA. A successful build, a Blender GIF and a
structural validator are different kinds of evidence. None certifies physical HOME.

## Centered lettering, candidate 36 (2026-11-13)

I requested both text lines centered separately under the central coin.
The generator centers each line by its visible alpha bounds at texture X=256.
Only the title texture pixels differ from candidate 27; all other CGFX bytes are
identical, including geometry, animation and background. Seven candidate and nine
static checks pass. This remains a standalone diagnostic; physical compatibility
is unverified and the production store remains the exact static recovery.

## Layout decision after left-side experiments (2026-11-13)

I explicitly chose the central layout again after left-side attempts
28–35 failed to keep the axis fixed in real HOME. These experiments were local
only and were not published. Two-model separation, translation compensation,
skeleton flags, rigid skinning and bone optimization flags did not establish a
working left-hand turn. They are archived under the ignored build directory.
The maintained generator and diagnostic selection are restored to candidate 27.
Do not claim left-hand rotation was solved or is a hardware impossibility.

## Isolated candidate 27: one continuous 3D coin (2026-11-13)

I rejected bitmap/sprite substitution and requested the normal native
rotation used by Minish Cap. Candidate 27 uses one rigid two-sided 3D coin above
an independent fixed wordmark. This diagnostic changes the previous horizontal
layout to Minish's central model / PNG-below arrangement; it is not a production
store layout approval. The change was stated in the conversation before building.

The coin is centered at X=Z=0, Y=3.8. Its seven native Transform curves use the
packaged Minish banner's 270-frame loop and StepLinear64 encoding: scale XYZ,
translation XYZ and continuous Y rotation from 0 to 2π. X/Z rotations are constant
zero; flags are `0x00400a00`, including the reserved Rotation-W absence repair.
The independent reader compares all scale, Y-rotation and X/Y-translation keys
exactly with the actual packaged Minish file. Z translation is deliberately zero
rather than Minish's -1 to put the axis on the scene center. There is no pose bank,
visibility animation or animated texture. The source path is `--minish-turn`.

Candidate 27 also creates opaque coin bones before transparent artwork, matching
Minish. Dictionary order and child/sibling traversal now agree; both validators
assert this. Candidate 26 had mismatched ordering and was never published. This
repairs a concrete structural difference, not a proven physical-freeze cause.

The rear has the same original 128×128 RGB565 logo, mapped to read correctly when
seen from behind. There are seven rigid meshes / 324 triangles and 224 KiB of
texture data. The complete CGFX is 262,696 bytes. The dotted background is at Z=-8
and scaled 1.42×, with the existing upper-fade inset of 12 texture pixels.
The English text remains fixed. The BNR audio is three seconds and does not loop.

Compilation and seven candidate host checks pass, together with the nine static
recovery checks. `home-banner-minish-reference.cs` independently reads the complete
candidate and its two faces. Real EUR HOME in the isolated Azahar profile was
captured through a complete turn; the app opens and exposes START to return HOME.
On app exit and icon reselection, some lower HOME UI disappears in Azahar.
The exact same defect was reproduced with the unmodified packaged Minish CGFX
in the same title/profile, so it is retained as an emulator limitation rather
than called a clean pass. Physical-console compatibility remains unverified. Do not call this a confirmed
repair for the historical freezes.

Tests 21–25 were rejected local experiments and were never published. They include
an orbiting child, a model-level offset that moved the entire composition, a
sprite atlas that I rejected, and geometry visibility experiments that HOME
did not play. Their tools are archived under the ignored build directory. They
are no longer selectable from the maintained generator.

The full store remains recovery 2026.11.12.14 with the exact static test-19 BNR.
The normal production build still rejects all animation resources. Candidate 27
uses the separate diagnostic title ID `000400000F0F5F00`; never replace the store's
selected resource based solely on this emulator result.

## Current recovery: store 2026.11.12.14 / exact static test 19

**Physical failure:** I installed full store 2026.11.12.13 and reported a
console freeze. Reject that CIA and its animated test-20 resource. Do not recommend
its QR again. The delivered CIA was checked: it contained exactly the intended
animated test-20 BNR; this was not an accidental different-resource package.
Matching Starfox's sparse channels did not prevent the failure. The complete
hardware root cause is still unknown. Passing Azahar/SPICA did not establish it.

Recovery 14 restores the *entire BNR* from physically working test 19, byte for
byte, including audio. There is no regenerated geometry, hybrid background or
animation. The appearance intentionally returns to test 19 as part of the recovery.
The model SHA-256 is `07df21c7250d09efefda7fac67e179f79873ded2cdc96e1d842ee26202a3272f`;
the complete BNR SHA-256 is `448c3804b04d7e041022c6eeeb3acd3e0b0a590aa9149108d3bfa62165eca3e0`.
The full store retains its title ID `000400000F0F5E00` and advances to build 14.

The normal build now requires `--require-static` on both its selected CGFX and
final packaged BNR. The gate rejects any animation resource. A regression recreates
an animated resource slot and the actual rejected store-13 resource is also checked
to fail. Experiments remain diagnostic files; do not re-enable animated production
banners based only on matching curves, source renders or emulator success.

The static test 19 has a positive user report. Full recovery 14 still needs its own
physical-console confirmation. Keep compilation, host, emulator and physical
claims separate. Do not describe this rollback as a diagnosed animation fix.

## Rejected experiment: Starfox sparse translation, test 20

I confirmed test 19 works on my physical console (model unspecified).
Preserve it as the approved static fallback. They now request Starfox's animation
system and a substantially larger fading background. After approving the final
layout in emulator HOME, they requested integration in the full store. The selected
model is now test 20; the static test-19 CIA remains an immutable recovery option.
The full local store version is 2026.11.12.13. Physical validation of its animated
banner is still pending.

A byte-level comparison found a difference missed by the test-18 curve-only check:
Starfox's CANM leaves all six real scale/rotation channels absent (flags `0x3f0000`,
zero payload), while test 18 replaced them with constants (flags `0x400fc0`, scale
1/1/1 and rotation 0/0/0). This is a confirmed difference, not yet a demonstrated
cause of the physical freeze. Test 20 removes those constant channels. It preserves
the reserved Rotation-W absence repair, giving `0x7f0000`. The used-channel flags
and absent payload now match Starfox; only that reserved bit differs.

All three float curves, their headers, segment encoding, interpolation, lookup
speed and 363 frame/value pairs compare byte-for-byte with the actual packaged
Starfox sculpted-v2 resource. The loop is four seconds. This is small translation,
not Euler rotation: keep the coin front-facing and do not promise a yaw wobble.
The coin's geometry/rest pose is test 19's straight front. The oval is scaled by
1.20 in each dimension about its existing center (40.32 × 20.16 model units).
The first local 1.65× preview was reduced at my request; a 1.35× check
still touched the sides, so the final candidate uses 1.20×. The large preview
approached HOME's status/camera controls. It was not published.
After I approved the sides/bottom and requested more clearance above,
the upper half of the alpha ellipse is inset by 12 texture pixels. Its lower
half is byte-identical to test 19, verified after decoding the serialized LA8.
The width, lower edge, 224 KiB texture budget, English lettering and audio stay unchanged.

```sh
3ds/tools/.venv/bin/python 3ds/tools/make-home-banner.py \
  --pycgfx 3ds/build/home-banner/pycgfx --starfox-model PATH \
  --starfox-motion --background-scale 1.2 --top-fade-inset 12 --out 3ds/build/home-banner/starfox-20
FORGE_BANNER_MODEL=3ds/build/home-banner/starfox-20/banner.cgfx \
  FORGE_BANNER_VERSION=20 python3 3ds/banner-test/build.py
python3 3ds/tests/test-home-banner-starfox.py --reference PATH
python3 3ds/tests/run-home-banner-reference.py \
  --spica 3ds/build/home-banner/research/SPICA --starfox-model PATH \
  --model 3ds/build/home-banner/starfox-20/banner.cgfx --background-scale 1.2
```

Nine host checks cover the top-only alpha change, exact reference curves/channels, package integrity,
unchanged texture/audio budgets and regressions recreating test-18 constants,
nonzero absent payload and a null translation curve. SPICA independently reads
all geometry and animation and checks the front face and expanded backdrop bounds.
Full store 2026.11.12.13 subsequently passed three HOME leave/return selections
and launched from HOME into the native catalog in Azahar 2126.2. The complete
CIA is delivered as a manual validation candidate; no official updater manifest,
website deployment or GitHub release is changed.
Neither parser success nor emulator success certifies physical HOME.

## Historical approved fallback: static front, test 19

On 2026-11-12 I viewed test 18 inside emulator HOME and chose a static
medallion facing directly forward. This supersedes the former wobble contract.
The generator now defaults to zero yaw and no animation resources. Keep the
English horizontal composition, fading oval and three-second original sound.
Test 19 has seven package/resource checks plus independent full-scene SPICA
verification of front-facing vertices and no animation. Test 19 loaded in Azahar
2126.2 real EUR HOME, with three leave/return selection cycles and fixed front view.
I subsequently confirmed test 19 works on my console; console model and
extended sleep/3D testing were not supplied.
Test 18 loaded in Azahar HOME but was reported to freeze physical HOME; those
results are distinct. The cause of the physical freeze is not established.

## Incident and historical status

On 2026-11-12, Forge's earlier banners orbited in physical HOME despite looking
correct in source previews. I then reported that selecting test revisions 13 and 14
froze the console. That is a **failed hardware result**, not “unverified success.”
Do not redistribute either rejected test or reuse their QRs as a recommendation.

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
hardware remediation. That 518,080-byte CIA was test 14 and it also froze physical HOME.

## Reproduced serialized defect and test 15

A second investigation compared commits `14cb234`, `6cc0f55`, `51a8f8c` and
`5be8161` and used SPICA's independent animation reader, revision `82acbd63695e4ef8b95a92ce884f25ec1044788f`.
It reproduced `EndOfStreamException` in each original CANM. Setting only bit 22
(`0x00400000`, Rotation W inexistent) made **each animation** deserialize successfully.
Full-scene SPICA import also encounters an unrelated emitter/resource-layout issue;
this experiment isolates CANM, not the whole scene or HOME renderer.

The pinned pycgfx `CANMBoneTransform` writes ten 32-bit channel fields:
`Sx Sy Sz Rx Ry Rz Rw Tx Ty Tz`. Its class treats Rw as padding and omits the
absence bit. Consequently flags advertise a nonconstant W curve whose pointer is
zero. Our old validator skipped the same field, so it missed the same defect.
See SPICA's [transform reader](https://github.com/gdkchan/SPICA/blob/82acbd63695e4ef8b95a92ce884f25ec1044788f/SPICA/Formats/CtrGfx/Animation/GfxAnimTransform.cs)
and [flag definitions](https://github.com/gdkchan/SPICA/blob/82acbd63695e4ef8b95a92ce884f25ec1044788f/SPICA/Formats/CtrGfx/Animation/GfxAnimTransformFlags.cs).

The wrapper now marks the unused field absent at serialization. It does not modify
the external converter. The production validator checks that bit and zero payload,
and rejects a null reference for any present nonconstant channel.

Test 15 differs from rejected test 14 in **one byte, one bit**, at model offset
`0x4366` (decimal 17254). Its flags change from `0x0000EBC0` to `0x0040EBC0`.
There is no simultaneous hierarchy, timing, geometry, texture, audio or icon-flag
change. Two regression tests recreate the missing bit and a null present Y curve.
The independent reader reads all nine intended channels, the 313 yaw keys, a closed
312-frame loop and the fixed position. The reversed one-bit mutation reproduces the
out-of-stream error.

This is a proven serialized-reader defect. It existed even in versions I
reported as loading, so it does **not** by itself prove the complete reason physical
HOME began freezing at revision 13. Any statement that those earlier versions were
fully valid, or that the pivot/extendedbanner change was the confirmed root cause,
would overstate the evidence. Test 15 still requires physical HOME confirmation.

Run the independent check after conversion, in addition to the normal gate:

```sh
# External research dependency, not part of the native toolchain.
python3 3ds/tests/run-home-banner-reference.py \
  --spica path/to/SPICA --dotnet path/to/dotnet
```

The harness links SPICA's unmodified parser source into an isolated .NET executable,
checks the corrected model, then recreates the exact missing-bit corruption and
requires the independent parser to fail. Generated files and report remain in
`3ds/build/home-banner/reference-reader/`; no emulator or hardware claim is made.

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
| Forge test 14, user-reported freeze | 263,432 | Medallion | Direct rigid node; explicit constant position and scale |
| Forge test 15 | 263,432 | Medallion | Same model as 14; missing Rotation-W absence bit corrected |

I approve Starfox's appearance. There are two different Starfox assets;
confirm which is packaged before adopting either wholesale. Its sculpted-v2
`fixed_parts.py` deliberately removes rotational animation from the billboarded
Arwing and retains small translation offsets. Therefore it is **not** proof that
YAxial plus authored Euler rotation will reproduce Forge's desired coin wobble.

ALttP and Doom do not exercise authored CANM playback in these files. They are
useful packaging/model references, but not equivalent animation test cases.
Some archived Doom model assets fail dictionary lookup checks while the native
source resource passes. Do not replace the packaged file with an older asset just
because the filename looks newer or more final. Worms is an investigation reference,
not an assumed hardware-safe baseline: I also reported problems there.

Read-only inventory tool:

```sh
python3 3ds/tools/audit-home-banner.py path/to/banner.cgfx
```

It reports model bones, parents, billboard modes, TRS and animation members. It is
an inventory, not a complete renderer or hardware validator.

## Authoring contract

Define desired screen-space behavior first: which parts stay fixed, which rotate,
the local pivot, angle range, period and expected silhouette. The current Forge contract is a static, straight-ahead coin. The previous ±24°
wobble contract is historical and was superseded by my test-19 decision.

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
The current 15 host tests cover packaged resources, malformed inputs, explicit TRS,
constant pivot, the reserved Rotation W slot, null present curves, SMDH flag omission
and CIA integrity. They do not emulate HOME.

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

Use the separate tiny test app for banner iterations. Do not make me reinstall
49 MB of store resources to test a few hundred KB of banner. Keep its identity
separate from the store. Do not update the store's public binaries until the banner
has passed the required review. Do not manufacture a new identity every attempt.

When publication is authorized and appropriate, pin the QR to an immutable commit,
download that URL, compare its size and SHA-256 with the reviewed CIA, and decode
the QR back to that exact URL. Record the embedded banner hash and test result.
Never say “fixed” based only on successful upload or compilation.

HOME may cache banners, but do not blame caching whenever I report a failure.
First inspect the delivered bytes and package metadata. Do not suggest deleting
HOME extdata, themes, saves or icon arrangements as routine troubleshooting.

## Open investigation

The exact relationship between the now-reproduced Rotation W defect and physical
HOME freezes, and the final screen-space wobble, remain unconfirmed. Missing
`extendedbanner`, TRS encoding, parent hierarchy and HOME's own presentation
transform must not be conflated with one another. Preserve rejected
CIA hashes privately and change one factor per diagnostic comparison where possible.
No new QR should be represented as a confirmed repair without the corresponding
HOME evidence. Keep reporting compilation, host tests, emulator and physical results
separately, including failures.

## Historical verification record for rejected test 14

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
- Physical HOME: revisions 13 and 14 failed on my console.
- Publication: I subsequently requested the tiny candidate CIA and its QR. Only the test binary is distributed; no store update, release or website deployment.

## Test 15 verification and delivery

- Native build: `FORGE_DEBUG=0 zsh 3ds/build.sh` passed (existing GNU-stack SDK warning).
- Tiny CIA: 518,080 bytes. Its ExeFS hashes, embedded banner and SMDH `0x125` flags
  pass the production gate. Its model SHA-256 is
  `3717abb0d7bd4b4554d8e5bfc8c5f0c2242dd11277ad7d0e5a493f3e1748221c`.
- Host: 15 tests pass against both the tiny CIA and the locally built store CIA.
- Independent CANM reader: SPICA reads the corrected data and rejects the same data
  with only the Rotation-W absence bit cleared. This is parser evidence, not HOME.
- Emulator: no new successful HOME validation. Physical: 13 and 14 are rejected by
  user report; 15 awaits feedback. The full store distribution remains unchanged.
- The publication consistency checker still reports the documented old store-build
  fingerprint mismatch; it has not been described as passing.
- The tiny candidate and source are published under the existing user request; no
  release or website deployment is created. The new QR pins an immutable CIA URL.

## Complete-scene reader repair / test 16

I again reported physical HOME freezing and could not identify the last
installed test number. Rejected 13/14 remain failed hardware evidence; this does
not establish whether 15 was tried. No earlier rejected CIA is recommended again.

A complete `Gfx.Open` plus `ToH3D` using SPICA reproduced a further format defect:
the pinned pycgfx writer emits fifteen DATA resource pairs, omitting FogAnimations
before Emitters. The first model dictionary then occupies the sixteenth pair.
SPICA attempts to interpret dictionary bytes as an emitter reference and fails.
The previous independent harness jumped straight to CANM and missed this.

Read-only comparison of the actual Minish Cap, Doom and ALttP native-source models,
and Starfox's ready-to-use model, found the same truncated table in all four.
Therefore this defect is **not a Forge-specific difference or a demonstrated sole
cause of the hardware freeze**. The successful reference titles show why a strict
reader failure cannot be substituted for a physical HOME diagnosis. No other port
was edited. Private paths, hashes and full reader errors are in
`.local/home-banner-repair/comparison.json` and the per-reference logs.

Forge's wrapper now supplies all sixteen pairs *before offset assignment*, keeping
the external converter unmodified. All self-relative offsets are regenerated by
its writer. The Rotation-W absence correction from 15 is retained. Geometry,
textures, audio, hierarchy, fixed position, 5.2-second timing and ±24-degree local
Y swing are otherwise unchanged. The model grows from 263432 to 263448 bytes due
to the added slot and alignment. The medallion geometry remains around its own
origin, with fixed node translation (-8.61, 1, .735), as confirmed by the independent
reader's decoded vertices. This does not establish HOME's presentation transform.

The production gate now validates all sixteen resource pairs, their counts,
dictionary references and DATA bounds. Two host tests check the full table and
recreate the old dictionary-in-emitter-slot corruption. The independent harness
now loads and converts the complete scene, decodes all six rigid meshes / 292
triangles, checks bone/index bounds and local coin geometry, then reads all nine
TRS channels and the closed 313-key yaw curve. It rejects both missing Rotation W
and the malformed resource table. Passing this remains file/parser evidence.

Test 16 uses the same test title ID `000400000F0F5F00`; its SMDH short name is
`Forge Banner Test 16` so the installed revision can be identified in HOME.
The local CIA is 518080 bytes, SHA-256
`6940904be99333fbbac2254957cdf2e5109ddad4e6b6411cefc4d18e244bdced`.
Its embedded model SHA-256 is
`65bed98313d838c8b0b4ac9fc96431fe0773d40a50c5d8f906428176788e055b`.

- Compilation: tiny CIA and normal store build passed; usual SDK GNU-stack warning.
- Host: 17 tests pass separately against tiny and store packages.
- Independent: full scene plus CANM checks pass on the model extracted from the
  **tiny CIA**; both malformed variants fail. ExeFS hashes, embedded BNR equality,
  title ID and embedded SMDH flags `0x125` are verified.
- Emulator HOME: no successful HOME evidence in this investigation; previous boot
  failure remains recorded. Application boot is not substituted for HOME.
- Physical: pending for 16; no claim that the console freeze is confirmed resolved.
- Delivery: immutable local copy in `3ds/build/banner-test/test-16/`; no new public
  release, source push, QR publication or store distribution update in this round.

## Physical video of test 16: appearance rejected

I recorded the console and reported that test 16 no longer crashes but the
medallion still orbits. The recording contains 448 frames at 30 fps (14.933 s).
All frames were extracted and automatically registered against the LCD; contact
sheets cover every frame. These are private under `.local/home-banner-video/`.
The yellow-colour tracker also detects the wordmark when the face disappears;
its centroid values are not valid measurements of the complete coin trajectory.

Visible observations: at 1.00 s the white reverse is at the right and covers the
wordmark; at 3.20 s the coin is nearly edge-on at the left; at 4.23 s the face is
visible at the left; at 11.00 s the reverse again covers the wordmark at the right.
The oval and lettering remain front-facing. The video therefore rejects the
claimed correspondence between the source GIF and physical HOME animation.
No freeze is observed in this short recording; that is not a general crash-free
certification. Test 16's appearance is rejected, not pending or approved.

The serialized local yaw remains bounded to ±24 degrees, with fixed translation;
that transform alone cannot expose the entire reverse or take the coin across
the wordmark. The generator explicitly gives the medallion BillboardMode.Off,
while the text and background use a facing mode. The physical motion is consistent
with HOME applying a further global presentation rotation to the non-billboard
piece. This is stronger evidence than the previous source-only pivot tests, but
it does not identify the exact firmware matrix order. A local-origin assertion
or full-scene SPICA import does not reproduce that presentation transform.

The next motion repair must handle the *rendered* medallion's facing/presentation
transform as well as the local pose. Repeating an empty-parent billboard change
without a demonstrated HOME result is not sufficient (earlier attempts already
failed). Keep test 16's serialization repairs and its CIA immutable. No new CIA
or GIF should be represented as reproducing physical HOME based on the authoring
camera alone. This video analysis changes evidence/documentation, not the model.

## Test 17: fixed-facing rigid poses

The Test 16 physical orbit is addressed by removing its skeletal rotation entirely.
`make-home-banner-poses.py` bakes nine actual rigid 3D medallion poses (-24 to +24
in six-degree increments) into vertices already positioned next to the wordmark.
The rendered medallion bone has identity TRS and exactly the same facing mode as
the lettering and backdrop. There is no translated animated bone whose position
can orbit under HOME's presentation transform. Adjacent poses crossfade using
native material Constant0 alpha tracks with source-over coverage compensation,
similar to the application's adjacent-pose drawing. No sprite movie is decoded.

The 5.2-second welcome easing, original textures, backdrop, lettering and three
second audio are retained. DATA's sixteen slots remain intact. No skeletal CANM
is emitted, so there is no Rotation-W slot or rotating-pivot track in this model.
The native file is about 403 KiB, below 512 KiB; 29 meshes / 2164 triangles share
the original 224 KiB of texture payload. Only two adjacent poses have nonzero
opacity at a sampled time. All meshes may still be submitted; these are resource
bounds, not physical CPU/GPU timings.

Build the isolated candidate without replacing the store's selected model:

```sh
3ds/tools/.venv/bin/python 3ds/tools/make-home-banner-poses.py \
  --pycgfx 3ds/build/home-banner/pycgfx
FORGE_BANNER_MODEL=3ds/build/home-banner/test-17/banner.cgfx \
FORGE_BANNER_VERSION=17 python3 3ds/banner-test/build.py
python3 3ds/tests/test-home-banner-poses.py
```

Seven package tests cover scene structure, facing, opacity, texture/audio budgets,
CIA integrity and damaged-table rejection. SPICA's full reader also checks the
complete scene, decoded mesh/index data, identity bone positions, matching facing
modes and all 27 closed material tracks. Its C# check is retained at
`3ds/tests/home-banner-poses-reference.cs`; compile with the same external SPICA
source/project setup as the existing reference harness. The existing Test 16
store resource remains selected until this diagnostic candidate is reviewed.

- Compilation: tiny Test 17 CIA passed, same test title ID and SMDH 0x125 flags.
- Host: seven candidate checks passed; the existing 17 Test 16 store checks still pass.
- Independent reader: complete scene and material-animation checks passed.
- Emulator HOME: no new successful evidence.
- Physical HOME: pending for 17; test 16 remains rejected for motion, with no freeze
  observed in the supplied 14.93-second recording.

Test 17's CIA and manifest are delivered as an immutable binary-only diagnostic
under `builds/banner-test/test-17/`. No full store update or release is published.
No source GIF is claimed to reproduce physical HOME playback.

## Fresh rebuild / test 18 — current candidate

**Physical rejection:** I reported that Test 17 freezes HOME. Its passing
host/SPICA checks did not establish HOME compatibility. Test 17 is rejected and
must not be recommended. I requested removal of the old implementation
and a rebuild from scratch based on Starfox.

The active `make-home-banner.py` was rewritten from scratch. It imports no prior
Forge scene/generator. The old generator, nine-pose implementation and its tests
are quarantined in `.local/home-banner-rejected/`. Old immutable CIAs remain
historical evidence, not selected build inputs. `3ds/meta/banner.cgfx` now selects
the fresh model. The store's public distribution is not updated in this round.

Reference selection was verified in Starfox's actual gameplay build script:
`platform/3ds/build-game.sh` selects sculpted-v2/output/StarFox-sculpted.cgfx.
The resource hash is `d8205544a852f2e3bac273f4cd949426bc7031cca54fa0003d02fe261c3691a3`.
Its converter fixes the rendered pieces' facing and retains only ArwingFlight's
small translation offsets. The new generator reads those exact three native
curves: 121 keys each, 240 frames / four seconds. No Starfox artwork is copied.
Independent SPICA parsing confirms all 363 new translation key times/values match
the packaged-reference resource exactly. This establishes matching data, not
matching hardware behaviour or absolute proof of firmware compatibility.

The new geometry is authored directly as a 40-sided medallion with a baked
18-degree pose, white bevel and grey edge. Its position is baked into vertices;
all three rendered bones have identity rest transforms and the reference's facing
mode. New background/wordmark textures are rasterized from shared brand/font
assets, retaining the oval fade, English subtitle and darker orange. The audio is
the shared three-second welcome sound. There are six meshes, 244 triangles,
229376 bytes of texture payload and a 262136-byte CGFX. There is one translation
member, no authored rotation and no material animation. The simpler float is
intentionally the reference's motion; it is not represented as the old GIF's yaw.

Regenerate with the pinned converter and the local reference model:

```sh
3ds/tools/.venv/bin/python 3ds/tools/make-home-banner.py \
  --pycgfx 3ds/build/home-banner/pycgfx --starfox-model path/to/StarFox-sculpted.cgfx
FORGE_BANNER_MODEL=3ds/build/home-banner/fresh-18/banner.cgfx \
FORGE_BANNER_VERSION=18 python3 3ds/banner-test/build.py
python3 3ds/tests/run-home-banner-reference.py \
  --spica 3ds/build/home-banner/research/SPICA --starfox-model path/to/StarFox-sculpted.cgfx
```

The native build uses the checked-in fresh resource. The gate has an explicit
fresh-scene profile, with finite bounded translation curves, zero rotation,
identity facing nodes and format/package checks. Eight host checks cover the
resource and rejected table/W-slot/rotation/facing mutations. Report native
compilation, host checks, independent parsing, emulator HOME and physical HOME
separately. Neither emulator HOME nor physical HOME has validated Test 18 yet.

Fresh 18 delivery checks: normal store and tiny native builds passed (usual SDK
GNU-stack warning). Eight production-gate host tests passed against each package.
Independent full-scene verification was run on the CGFX extracted from the tiny
CIA, after ExeFS hash and exact BNR equality checks. All 363 translation key times
and values compare exactly with the selected Starfox resource. The new design
render is layout evidence only. Tiny CIA: 514496 bytes, SHA-256
`a44430f2423725c34b9ba895f510983a03f6550a7ad4d51474dd1a09f90933d0`.
Physical HOME and emulator HOME remain unvalidated for this candidate.
