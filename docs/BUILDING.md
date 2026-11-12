# Building and testing

## Normal native build

The repository includes reviewed RomFS assets. Do **not** use `--pack` for an ordinary
build: that option replaces RomFS from intermediate rendering output and must only be used
as part of a complete asset regeneration.

Dependencies:

- devkitARM and libctru; Citro2D, Citro3D and tex3ds development support.
- 3DS portlibs: curl, mbedTLS, libarchive, Jansson, FreeType, PNG, JPEG, GIF, Theora,
  Ogg, bzip2, LZMA, LZ4, Zstandard and zlib.
- devkitPro tools: `3dsxtool`, `smdhtool`, `bannertool`, `makerom`.
- Python 3, CMake and zsh on the build host.

The verified local toolchain uses devkitARM GCC 16.1.0. libwebp 1.6.0 is pinned in
`3ds/vendor/` and compiled offline as a decoder-only static library by `build-webp.sh`.
The build handles paths containing spaces and uses the installed SDK; it does not upgrade it.

```sh
export DEVKITPRO="$HOME/devkitpro"   # or your installed SDK root
export DEVKITARM="$DEVKITPRO/devkitARM"
zsh 3ds/build.sh
```

Outputs: `3ds/build/forge-store.cia`, `.3dsx`, `.elf`, `.smdh`, a linker map and `manifest.json`.
Every build rechecks the media manifest. Public builds use `FORGE_DEBUG=0` by default.
Changing compiler/debug flags invalidates cached objects. Do not share a changing executable
with an active emulator: the runner copies an immutable execution image before launch.

## Host validation

Create a local environment for image/font/decoder fixtures:

```sh
python3 -m venv 3ds/tools/.venv
3ds/tools/.venv/bin/pip install -r 3ds/tools/font-requirements.txt
```

The video harness also needs FFmpeg, a host C compiler, pthreads, libtheoradec, libogg and
libjpeg-turbo (on macOS: Homebrew packages `ffmpeg theora jpeg-turbo`). Installer/catalog
harnesses need host Jansson and libarchive. Some host transport shims currently use macOS
CommonCrypto, so the full queue suite is macOS-specific.

```sh
python3 tools/check-repository.py
python3 3ds/tests/catalog-host.py
python3 3ds/tests/run-install-integrity.py
python3 3ds/tools/check-file-view.py
3ds/tools/.venv/bin/python -I 3ds/tools/check-media.py
3ds/tools/.venv/bin/python -I 3ds/tests/run-video-test.py --all
```

The video test compiles the actual production decoder against a host platform shim and
compares output with FFmpeg; it covers malformed files, seek, overload, pause, stop and
shutdown with address/undefined-behavior/thread sanitizers. Other `3ds/tools/check-*` tools
cover fonts, tabs, rich text, audio evidence and Universal conversion; read their CLI help.

## Emulator and hardware

```sh
FORGE_DEBUG=1 zsh 3ds/build.sh
3ds/tools/.venv/bin/python -I 3ds/tools/azahar_run.py \
  --name smoke --seconds 40 --input '240:kA;400:shome;450:q'
```

Set `FORGE_AZAHAR` to your Azahar executable if it is not at the runner's macOS default.
Use `--new3ds` for the New model, and `--seed-media` only for an explicitly labeled cached
playback test. A real first-download test must start without a seeded cache. The runner
creates an isolated SD/profile; it never runs against the user's SD. It may copy the locally
available DSP firmware into that isolated profile, but that firmware is never published.

SELECT dumps in debug builds contain private memory. Keep them local. An upload endpoint
is opt-in via `FORGE_DUMP_URL`; the distributed build must leave it unset.

## Asset regeneration

- Exact Inter fonts: `3ds/tools/render-fonts.py`, `render-fonts.mjs` and `FONTS-V2.md`.
- Home icons: `render-home-icons.py`; Universal: `pack-universal-art.py`.
- Native previews: `pack-video.py` → review output → `check-media.py`; see [Media](MEDIA.md).
- Browser-based art/audio renderers are offline authoring utilities, not build dependencies.
  The generated RomFS is the normal build input. In Code Atlas, use its preview tools for
  browser inspection and do not replace them with standalone automation.
- `tools/prepare-assets.sh` is a legacy local import helper; it requires my other local source
  projects and is not needed to build or run this repository.

Before publishing a build, return to `FORGE_DEBUG=0`, rebuild, run repository checks and use
`tools/package-build.py` to copy exactly the reviewed binaries and hashes into `builds/3ds/`.
