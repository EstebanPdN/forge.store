# Native HOME banner recovery

forge.store 2026.11.12.14 restores the entire static banner from test 19,
which I confirmed works on my physical console. The final full-store CIA
contains that exact BNR byte for byte, including the original three-second audio.
The 3D medallion faces forward; there are no animation resources. Its original
smaller oval/background is deliberately restored unchanged for recovery.

**Rejected:** I reported that animated full store 2026.11.12.13 froze the
console. Its intended test-20 banner was present in the delivered CIA. Matching
Starfox curves/channels and passing Azahar/SPICA did not prevent that failure.
Do not recommend the version-13 QR or describe its animation as fixed.

Read [HOME banner engineering guide](HOME-BANNER-GUIDE.md) before any changes.
The exact animation-related hardware cause is still unknown. This is a rollback
to the known working resource, not another animation experiment.

## Selected resource and package

- CGFX: 258,904 bytes, SHA-256 `07df21c7250d09efefda7fac67e179f79873ded2cdc96e1d842ee26202a3272f`.
- Whole BNR SHA-256: `448c3804b04d7e041022c6eeeb3acd3e0b0a590aa9149108d3bfa62165eca3e0`.
- Full CIA: 49,447,872 bytes, SHA-256 `30db77fdd9294975461eaa9408e0ce2a81c5350dea06b327ed044ebd7b33112c`.
- Full store title ID `000400000F0F5E00`; static test-19 title ID `000400000F0F5F00`.
- Six rigid meshes / 244 triangles; forward face and identity facing bones.
- Three textures / 229,376 bytes: RGB565 logo, RGBA4 lettering, LA8 oval.
- Zero skeletal, material or other animation resources.
- Stereo PCM16, 32 kHz, 96,000 samples per channel, nonlooping, three seconds.

## Production recovery gate

`3ds/build.sh` now uses `--require-static` for both the CGFX and packaged BNR.
Any animation resource stops the normal build. The actual failed version-13
resource is rejected by this policy. Source/render previews do not override it.

```sh
FORGE_DEBUG=0 zsh 3ds/build.sh
python3 3ds/tests/test-home-banner.py
```

Do not regenerate the approved recovery asset just to make another preview. Its
bytes were recovered and compared against the immutable test-19 CIA. Diagnostic
motion experiments remain available separately but are not production inputs.

## Evidence

- Compilation: full native store passed, no `--pack`; existing SDK GNU-stack warning.
- Host: nine banner/package/policy checks passed, including rejection of an
  animation resource. ExeFS hashes, SMDH `0x125`, store title ID, and exact BNR
  equality with test 19 were verified in the full CIA.
- Independent SPICA: complete scene read passed; front-facing geometry and zero animations.
- Production installer, texture queue, music and video harness results are recorded
  under `3ds/build/home-banner/store14-*.log` separately from emulator evidence.
  The first video run failed a seek PCM assertion at `video-test.c:357`; no
  decoder code was changed. A complete repeat with preserved fixtures passed
  ASan/UBSan, TSan and all seven encoded previews. The initial failure is retained
  as an intermittent host result, not silently counted as a pass.
- Emulator HOME: Azahar 2126.2, isolated real EUR HOME; three leave/return
  selection cycles passed on the full store. The installed BNR was compared with
  test 19, and launching reached Welcome and the main catalog.
- Physical console: static test 19 works by user report; full recovery 14 pending.
  The preceding animated full store 13 is a failed physical result.

The immutable local delivery image is `3ds/build/store-recovery-14/`. Private
logs/captures stay in `.local/`. No website or GitHub release deployment is involved.

Manual recovery CIA/manifest: `builds/banner-test/store-recovery-14/`. The old
store-13 manifest is marked rejected. No official updater manifest is changed.
