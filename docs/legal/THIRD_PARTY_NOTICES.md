# Third-party notices

This repository does not relicense third-party components. Their original terms control.
The lists below describe the maintained sources and the SDK libraries used by the build.

| Component | Use | Notice / upstream |
| --- | --- | --- |
| libwebp 1.6.0 | Native image decoder, vendored source | [COPYING](../../3ds/vendor/libwebp-1.6.0/COPYING) and adjacent PATENTS/AUTHORS |
| NanoSVG | Bounded native SVG subset | [Notice](../../3ds/source/vendor/LICENSE-NanoSVG.txt) |
| html-to-image | Web pixel magnifier/export | [MIT notice](../../src/js/vendor/html-to-image.LICENSE) |
| Inter | Native/web type | [SIL OFL](../../assets/fonts/inter/OFL.txt) |
| DM Sans | Branding type | [SIL OFL](../../assets/fonts/dmsans-OFL.txt) |
| Nunito | Preserved/reference typography | [SIL OFL](../../assets/fonts/nunito-OFL.txt) |
| Space Mono | GitHub code/terminal text | [SIL OFL](../../assets/fonts/space-mono/OFL.txt) |
| libctru, Citro2D, Citro3D | Nintendo 3DS services and GPU | [devkitPro](https://github.com/devkitPro) — individual project licenses |
| Theora, Ogg | Native compressed preview decoding | [Theora](https://github.com/xiph/theora/blob/main/COPYING), [Ogg](https://github.com/xiph/ogg/blob/main/COPYING) — BSD-style |
| libjpeg-turbo | Native JPEG and legacy video decode | [License](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md) |
| libpng, zlib | Images/compression | [libpng](https://github.com/pnggroup/libpng/blob/libpng16/LICENSE), [zlib](https://github.com/madler/zlib/blob/develop/LICENSE) |
| giflib | GitHub image decode | [Project](https://sourceforge.net/projects/giflib/) — MIT |
| FreeType | SVG text | [Licenses](https://freetype.org/license.html) — FreeType License/GPL alternative |
| curl, mbedTLS | HTTPS | [curl](https://github.com/curl/curl/blob/master/COPYING), [mbedTLS](https://github.com/Mbed-TLS/mbedtls) |
| Jansson | JSON | [MIT notice](https://github.com/akheron/jansson/blob/master/LICENSE) |
| libarchive | Install archives | [Notice](https://github.com/libarchive/libarchive/blob/master/COPYING) |
| bzip2, xz/liblzma, LZ4, Zstandard | Linked archive codecs | Original SDK package terms apply; retain their notices when redistributing modified packages. |

HandBrake and FFmpeg are **offline authoring tools**, not bundled executables. Python/font
and browser rendering tools are also build/development dependencies, not console binaries.

The reviewed font atlas includes fallback glyphs generated during earlier local development
from installed system fonts. The exact generation process and limitations are recorded in
`3ds/tools/FONTS-V2.md`; no blanket permission to reuse those glyphs is asserted. Font
sources with explicit OFL terms are kept alongside their license texts. Regeneration on
another platform may need an explicitly supplied fallback font set.

Game artwork, trailers, screenshots, names and trademarks retain their original rights.
Consult [the asset register](../ASSET-CREDITS.md); credits do not establish permission.
