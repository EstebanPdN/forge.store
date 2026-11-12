# Native HOME banner

The CIA packages `3ds/meta/banner.cgfx`, with the shared Forge icon, horizontal
English wordmark and subtitle, and an oval background that fades from 35% of its
radius to its transparent edge. `3ds/meta/banner.png` is the previous flat reference.
The application screens and runtime code are unchanged.

Version 2026.11.12.12 adds an identity `MedallionFacing` parent using YAxial billboarding
to isolate the child from HOME scene rotation. This remains a hardware test candidate.

The medallion geometry is centered at its own local origin, with a fixed node position.
It uses the welcome screen's 5.2-second ±24° Y rotation and cubic-bezier(.42,0,.58,1)
easing, sampled at 60 Hz. There is no animated translation compensation, vertical bob
or scale change. HOME rendering on physical hardware still needs confirmation.

The complete composition is uniformly enlarged by 5% about its center. The wordmark
baseline is raised 12 texture pixels to separate its descenders from the subtitle.
The title is about 19% larger than the initial lettering, with both font weights sharing one baseline. Its
512×128 RGBA4 texture is explicitly restored after conversion, avoiding the converter's
256px width cap. This is pre-rasterized lettering, not a claim of hardware pixel-perfect
sampling; HOME owns final projection/filtering.

## Resource budget

- 292 authored triangles, six serialized meshes / draws, three rigid rendered bones.
- Three textures: 128×128 RGB565 logo, 512×128 RGBA4 lettering, 256×128 LA8 oval.
- Texture payload: 229,376 bytes (224 KiB). LA8 preserves 256 alpha levels for the
  white oval; RGBA8 is not used. The text has a separate baseline and clear gap.
- CGFX: 276,184 bytes, below the 524,288-byte HOME limit.
- Animation: one member, six scalar curves, 313 keys per authored track,
  312 native frames, matching endpoints and strictly positive scales.
- Audio: the original welcome sound, stereo PCM16, 32 kHz, exactly 96,000 samples
  per channel / three seconds, nonlooping, with a half-second fade out.

These are resource bounds, **not measured frame times**. No runtime blur, video
stream, texture upload loop, allocation loop or application worker is introduced.
HOME owns drawing and audio playback. Only physical HOME testing can establish
performance, stereo comfort and selection/suspend behaviour on Old and New 3DS.

## Regeneration

Normal builds need only the checked-in CGFX/WAV and the standard toolchain.
Authoring additionally needs Pillow, fonttools and gltflib 1.0.13 in the existing
`3ds/tools/.venv`, FFmpeg, and an external MIT-licensed pycgfx checkout at
`6c2e0cd53a5f4c3265b9aa9a1f30806f8291116e`.

```sh
git clone https://github.com/skyfloogle/pycgfx.git 3ds/build/home-banner/pycgfx
git -C 3ds/build/home-banner/pycgfx checkout 6c2e0cd53a5f4c3265b9aa9a1f30806f8291116e
3ds/tools/.venv/bin/python 3ds/tools/make-home-banner.py \
  --pycgfx 3ds/build/home-banner/pycgfx
```

The generator writes into `3ds/build/home-banner/generated/`. Review its model,
textures, audio and report before copying `banner.cgfx` and `banner.wav` to
`3ds/meta/`. The converter stays external and unmodified. The wrapper rebuilds
its sorted bone dictionary, fills explicit mesh-to-bone names, configures unlit
materials and encodes the LA8 oval in PICA's 8×8 Morton order.

```sh
zsh 3ds/build.sh
python3 3ds/tests/test-home-banner.py
python3 3ds/tests/run-install-integrity.py
```

The standard build checks serialized dictionary traversal, bone bindings,
non-singular transforms, curve values, supported textures and audio bounds. It
then verifies the CIA's ExeFS header/file SHA-256 hashes and compares its embedded
banner byte-for-byte with the selected BNR. The tests also reject damaged
resources and compare the packaged PCM samples with the input WAV.

## Evidence, 2026-11-12

- Compilation/package: passed with `zsh 3ds/build.sh`, no asset-wide `--pack`.
  The SDK linker reports its existing `crtn.o` GNU-stack note warning.
- Host: eleven banner checks and 27 production installer scenario groups passed.
- Preview: rendered from the same glTF geometry, textures and animation in
  Blender at the HOME camera; GIF/MP4 are design evidence, not HOME captures.
- Emulator HOME: not validated. Launching a 3DSX would not exercise this resource.
- Physical Old/New 3DS: not validated. Select the icon for several complete loops,
  switch repeatedly between titles, check sound ends at three seconds, test 3D
  off/on and sleep/resume on both models before declaring it lag-free.
- Version 2026.11.12.9 packages this banner in the distributable CIA alongside the
  existing store fixes and refreshed emulator artwork. The build validates the embedded
  banner and ExeFS hashes before packaging.

Local build reports are `3ds/build/home-banner-model.json` and
`3ds/build/home-banner-package.json`; private logs stay in `.local/home-banner/`.

Format references: [pycgfx](https://github.com/skyfloogle/pycgfx),
[CBMD](https://www.3dbrew.org/wiki/CBMD),
[texture formats](https://github.com/skyfloogle/pycgfx/blob/6c2e0cd53a5f4c3265b9aa9a1f30806f8291116e/cgfx/txob.py).
