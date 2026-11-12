#!/bin/sh
set -eu
cd "$(dirname "$0")"
# Override FT_CFLAGS/FT_LIBS for non-Homebrew systems.
FT_CFLAGS=${FT_CFLAGS:-"-I/opt/homebrew/include/freetype2"}
FT_LIBS=${FT_LIBS:-"-L/opt/homebrew/lib -lfreetype"}
${CC:-cc} -std=c11 -g -O1 -Wall -Wextra -Wno-unused-function -Wno-sign-compare \
    -fsanitize=address,undefined $FT_CFLAGS ../remote_svg.c remote_svg_test.c \
    $FT_LIBS -lm -o remote_svg_test
./remote_svg_test profile-card-dark.svg profile-card-light.svg
