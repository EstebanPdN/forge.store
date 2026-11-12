#!/bin/sh
# Public fixture paths may be supplied as arguments. No network access in tests.
set -eu
cd "$(dirname "$0")"
out=$(mktemp -d "${TMPDIR:-/tmp}/forge-remote-image.XXXXXX")
trap 'rm -rf "$out"' EXIT HUP INT TERM
export PKG_CONFIG_PATH="/opt/homebrew/opt/freetype/lib/pkgconfig:/opt/homebrew/opt/libpng/lib/pkgconfig:/opt/homebrew/opt/jpeg-turbo/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I/opt/homebrew/opt/giflib/include $(pkg-config --cflags freetype2 libpng libjpeg libwebp) \
  remote_image_test.c -o "$out/test" $(pkg-config --libs freetype2 libpng libjpeg libwebp) \
  -L/opt/homebrew/opt/giflib/lib -lgif -lz -lm
ASAN_OPTIONS=detect_leaks=0 "$out/test" "$@"
