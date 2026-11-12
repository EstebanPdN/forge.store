# html-to-image (local capture only)

Vendored UMD build of html-to-image 1.11.13, MIT license in `html-to-image.LICENSE`.
Source: https://github.com/bubkoo/html-to-image/tree/v1.11.13

Three deliberate local changes for 3DS pixel inspection:

- Preserve the exact computed font size; remove the upstream floor(size) − 0.1 px adjustment.
- Clone SVG descendants individually so their computed CSS (rings, gradients, icons) is retained.
- Resolve SVG fragment paint references inside the captured SVG, instead of referring to the original HTML URL.

The unavailable source-map footer is removed. This file is imported only by the web preview. Nothing is uploaded: fonts, artwork and rasterization remain in the browser.
