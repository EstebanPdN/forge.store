# Native store artwork

The 3DS application is the active target. The web prototype is retained as historical reference;
this update does not depend on or deploy it.

- SNES, PlayStation, GBA, Nintendo 64 and Mega Drive: 15 distinct original covers per grid.
  `assets/community/cover-sources.json` records titles, source URLs and hashes.
  `tools/prepare-cover-backgrounds.py` renders the reviewed grids and bakes their soft blur.
- RetroArch: the official **bichromatic pads / MENU** wallpaper from
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
