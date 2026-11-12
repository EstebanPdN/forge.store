# Native V2 font/assets workflow

The font renderer is offline: Pillow/FreeType at native pixel sizes, fontTools for
coverage checks, and CairoSVG for UI SVG strokes. `render-fonts.mjs` refreshes the
eight source locales into `build/gen/font-strings.json`, exports the local SVG
object, and invokes Python. It never starts a browser or server. Install Python
requirements into `3ds/tools/.venv` from `font-requirements.txt`; Cairo must be
installed locally (the wrapper includes Homebrew library paths on macOS).

From the repository root:

```sh
node 3ds/tools/render-fonts.mjs .
3ds/tools/.venv/bin/python -I 3ds/tools/pack.py . --fonts-only
3ds/tools/.venv/bin/python -I 3ds/tools/check-fonts.py .
python3 3ds/tools/check-gfx-text.py
```

`--fonts-only` updates `romfs/gfx/fonts.bin`, `kerning.bin` and `glyphs-*.ftx`; it does not
clear romfs, compile, link, or create a CIA/3DSX. FNT3 adds an explicit line-height
field to every style and must ship with the updated gfx.c loader. The old FNT2
binary is intentionally not accepted by the V2 loader. Do not replace font
atlases underneath a running process.

Inter comes from exactly `assets/fonts/inter/Inter-var.ttf`. Brand text comes
from the locally distributed DM Sans WOFF2 subsets. Native UI styles include
9/12, 10/14, 11/15 at weights 400/500/600/700, and 14/18 and 15/18 headings.
`text`, `text_dyn`, `text_w`, and `text_wrap` default to Inter 500;
`text_caps` defaults to 900. Styles also include weights 800/900 for the web’s strong roles. Explicit style calls select the exact baked size
first, then the closest numeric weight. DM Sans 10 px 400/750 covers the status
wordmark. Letter spacing remains in native pixels.

Coverage includes all locale values, Forge and Universal JSON values, C/H/INC
sources including panels.inc, and uppercase characters. The non-display
`search_fold[]` normalization data is excluded. Fallback glyphs are selected
from local Hiragino Sans, Hiragino Sans GB, Apple Symbols, Apple Color Emoji,
and ArialHB; `FORGE_FALLBACK_FONTS` overrides this colon-separated list on another
machine. Font/source hashes and actual fallback use appear in font-report.json.
The current corpus has 753 codepoints and zero missing-source replacements.

Remote unknown scalars use U+FFFD then `?` consistently for draw, width and wrap.
Malformed UTF-8 advances safely. This remains a per-scalar renderer: no contextual
shaping, bidirectional layout, or composed emoji sequences. ASCII pairs and adjacent corpus pairs use a sorted KRN1 sidecar; draw, width and wrapping apply the same fractional advances and pair deltas. RAQM preserves fractional advances instead of rounding each scalar. Variation
selectors and joiners do not add ink or advance. Color emoji are A8 silhouettes.

`DRAW_X` and `DRAW_Y` apply once at low-level gfx draw calls (including text,
shapes, images, rotations) and scissor. Reset both after each translated panel.
Do not also use C2D_ViewTranslate for those offsets. Raw C2D calls made outside
gfx.c must apply their own offset or go through a gfx wrapper.

The home icon generator is separate: see build/gen/icons-44/report.md for its
working command, source order, checks, renderer limits and catalog integration.
pack.py's full asset mode now includes the generated icons-44 PNGs. Catalog
integration is owned elsewhere: add 44 to ICON_PX/ICON_SZ and their loops, or
load icons-44.ftx as a Sheet and use the Forge App.icon index. Universal art
continues through its existing separate sheet path.

Evidence under build/gen:
- font-report.json: coverage and input hashes.
- font-pack-report.json: 86 styles, 48,608 records; three 1024×1024 and one 1024×512 A8 pages, 3.5 MiB texture. Includes DM Sans 800 30/30 for Universal wordmarks.
- font-check.json: all packed glyph pixels/metrics/bounds and binary hashes.
- font-specimen.png: light/dark 1:1 sample composed from the binary atlas.
- gfx-text-check.log: host ASan/UBSan tests on the actual extracted text code,
  including malformed UTF-8 at a protected page boundary, replacement width/wrap,
  >64 KiB paragraph offsets, missing atlas, and XY text translation.
- gfx-compile.log and gfx-v2.o: isolated devkitARM GCC 16.1.0 compile of gfx.c.

Offline rasterization is not byte-identical to Chromium. Native-size emulator comparisons and builds now exist under `docs/native-v2-parity/`; they report color-tolerant pixel scores and SSIM, with original images and differences. Hardware appearance/performance still requires a physical console. The complete port remains in progress.

Space Mono Regular/Bold 10 px supplies the native monospaced code role. It is an
OFL font stored under assets/fonts/space-mono with its license; it is not the same
outline as the web platform's system Menlo fallback. Total A8 texture use stays
3.5 MiB. The 86 styles include these two additional mono styles.
