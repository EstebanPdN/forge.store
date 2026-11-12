# Native store artwork

The 3DS application is the active target. The web prototype is retained as historical reference;
this update does not depend on or deploy it.

- SNES, PlayStation, GBA, Nintendo 64 and Mega Drive: 15 distinct original covers per grid.
  `assets/community/cover-sources.json` records titles, source URLs and hashes.
  `tools/prepare-cover-backgrounds.py` renders the reviewed grids and bakes their soft blur.
- RetroArch: the official illustrated **retroarch_nologo_bg** wallpaper chosen for this build from
  [retroarch-assets](https://github.com/libretro/retroarch-assets), by its contributors,
  CC-BY-4.0. Resized and cropped by the native banner packer.
- FourthTube, Wumiibo, Nimbus, Anemone and Artic: flat vector backgrounds. Their SVG sources
  accompany the WebP inputs; no photorealistic AI backgrounds are included.
- Checkpoint and Nimbus: larger official icons. FourthTube: a vector recreation of the
  existing play symbol and wordmark, retaining the green/white/black palette.
- DaedalusX64: AI-assisted restoration of the existing wordmark using the built-in image tool.
  Final prompt: “Restore this exact DaedalusX64 wordmark at high resolution. Preserve the
  exact text DaedalusX64 and its tall narrow angular letter shapes and spacing. White letters
  with a crisp black outline, transparent background. Flat clean edges without pixelation,
  no glow, no shadow, no additional symbols. Wide horizontal wordmark tightly framed,
  suitable for downsampling to 200px wide in a Nintendo 3DS UI.”
  Saved in `assets/community/daedalus/logo.webp`, sampled once into the native atlas.

Native white banner titles have a one-pixel black outline. The generated backgrounds cover
both stereo eye views throughout the existing drift animation. Red Viper Color, Universal
Updater and FTPD keep their approved artwork.

## Emulator icon restorations — 2026.11.12.7

PSX3D, DaedalusX64 and PicoDrive now use 512×512 source icons. These are AI-assisted
restorations of the original artwork, not newly discovered official high-resolution originals.
The built-in `image_gen` tool was used, with the existing icon supplied as the reference.
The native packers sample them into the existing 44px grid and 174px foreground sprites.
No texture dimensions or runtime memory budgets increase.

Final shared prompt for the three icons:

> Restore this exact small emulator icon as a crisp high-resolution square app icon.
> Faithfully preserve its identity, palette, arrangement and symbols. [Specific direction below]
> Clean vector-like edges and flat shading, no pixelation, no photographic texture,
> no extra elements. Fill the square with the existing background.
> Do not add rounded outside corners; the app masks those itself.

Specific directions and saved files:

- `assets/community/psx3d/icon.webp`: Keep exact text PSX on first line and 3D below,
  white and pale pink rounded geometric letters, purple background, orange-yellow striped sunset above.
- `assets/community/daedalus/icon.webp`: Keep the exact isometric interlocking block emblem
  with yellow top faces, green left faces, blue right faces and red inset on its dark blue
  background. No text.
- `assets/community/picodrive/icon.webp`: Keep the blue Sega-style gamepad illustration
  with circular directional pad on left, circular buttons on right, and exact white text
  3DS below on the blue gradient background.

The original mGBA handheld icon stays unchanged. The separate wordmark is saved at
`assets/community/mgba/logo.webp`, with transparency, using the built-in tool and this prompt:

> Extract and faithfully reconstruct only the mGBA wordmark visible inside the screen
> of this purple handheld icon. Exact text: mGBA (lowercase m, uppercase G B A),
> chunky geometric squared letters like the source, white/light gray lettering with
> a thin black outline. Make the lettering face straight ahead, no perspective.
> High-resolution clean vector-like edges; preserve the intentional square-letter style
> without blurry pixels. Transparent background. No handheld, no screen,
> no scattered background squares, no extra words. Wide tightly framed standalone wordmark.
