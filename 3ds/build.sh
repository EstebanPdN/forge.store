#!/bin/zsh
# Builds the native 3DS store without make (the project path has spaces):
#   3ds/build/forge-store.elf / .smdh / .3dsx (romfs inside) and forge-store.cia
# The romfs comes from 3ds/tools/pack.py; HOME Menu art from 3ds/tools/render-meta.mjs.
# Usage: 3ds/build.sh [--pack]   (--pack rebuilds 3ds/romfs first)
set -e
D="${0:A:h}"
P="${D:h}"
export DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
B="$DEVKITARM/bin/arm-none-eabi"
T="$DEVKITPRO/tools/bin"
OUT="$D/build"
OBJ="$OUT/obj"
mkdir -p "$OBJ"
cd "$D"
if [[ $1 == --pack ]]; then
  tools/.venv/bin/python -I tools/pack.py "$P"
fi
# Videos are remote assets, never embedded in an installable build.
if [[ -n $(find "$D/romfs" -name '*.fvid' -print -quit) ]]; then
  echo "Move encoded videos to 3ds/media before building." >&2
  exit 1
fi
python3 "$D/tools/media-manifest.py"
python3 "$D/tools/stage-romfs.py"
ARCH=(-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft)
zsh "$D/tools/build-webp.sh" > "$OUT/webp-build.log" 2>&1
# Public builds omit memory dumps and the watchdog. FORGE_DEBUG=1 enables testing:
# SELECT writes a local memory/screen dump and shutdown leaves a trace.
# FORGE_DUMP_URL=http://<mac>:<port>/<token> also sends each dump to tools/dump-server.py.
DEBUG_FLAGS=(-DFORGE_DEBUG=${FORGE_DEBUG:-0})
[[ -n $FORGE_DUMP_URL ]] && DEBUG_FLAGS+=("-DFORGE_DUMP_URL=\"$FORGE_DUMP_URL\"")
CFLAGS=($ARCH -O2 -g -std=gnu11 -Wall -Wno-unused-parameter -Wno-missing-field-initializers $DEBUG_FLAGS
        -DFORGE_BUILD_UNIX=$(date -u +%s) -mword-relocations -ffunction-sections -fdata-sections -D__3DS__
        -I source -I "$D/vendor/libwebp-1.6.0/src" -I "$DEVKITPRO/portlibs/3ds/include/freetype2" -I "$DEVKITPRO/libctru/include" -I "$DEVKITPRO/portlibs/3ds/include")
# Compiler flags are part of the cache key; debug/public builds must never mix objects.
CONFIG="${CFLAGS:#-DFORGE_BUILD_UNIX=*}"
RECOMPILE=0
[[ ! -f "$OUT/compile-config.txt" || "$(cat "$OUT/compile-config.txt")" != "$CONFIG" ]] && RECOMPILE=1
OBJS=()
PIDS=()
for f in source/*.c; do
  o="$OBJ/${${f:t}%.c}.o"
  OBJS+=("$o")
  newer=0
  for hdr in source/*.h source/*.inc source/vendor/*.(c|h|inc); do [[ "$hdr" -nt "$o" ]] && newer=1; done
  # network.c carries the build time; main.c and debug_dump.c the test-build switches.
  if [[ "$f" == source/network.c || "$f" == source/main.c || "$f" == source/debug_dump.c || ! -f "$o" || "$f" -nt "$o" || $newer == 1 || $RECOMPILE == 1 ]]; then
    "$B-gcc" $CFLAGS -c "$f" -o "$o" &
    PIDS+=("$!")
  fi
done
for pid in $PIDS; do wait "$pid" || exit 1; done
print -r -- "$CONFIG" > "$OUT/compile-config.txt"
for o in $OBJS; do [[ -f "$o" ]] || { echo "missing $o"; exit 1; }; done
"$B-gcc" -specs=3dsx.specs $ARCH -g -Wl,--gc-sections $OBJS -L "$DEVKITPRO/libctru/lib" -L "$DEVKITPRO/portlibs/3ds/lib" \
  -Wl,--wrap=mbedtls_x509_time_is_past,--wrap=mbedtls_x509_time_is_future -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -larchive -ljansson -lbz2 -llzma -llz4 -lzstd -lz \
  -ltheoradec -logg -lcitro2d -lcitro3d -lctru -lfreetype -lpng -ljpeg -lgif "$OUT/webp/libwebpdecoder.a" -lbz2 -lz -lm -Wl,-Map,"$OUT/forge-store.map" -o "$OUT/forge-store.elf"
"$T/smdhtool" --create "forge.store" "Ports for Nintendo 3DS" "Esteban's Forge" meta/icon.png "$OUT/forge-store.smdh"
"$T/3dsxtool" "$OUT/forge-store.elf" "$OUT/forge-store.3dsx" --smdh="$OUT/forge-store.smdh" --romfs="$OUT/romfs"
# CIA for the HOME Menu (banner sound: the intro's welcome chime)
"$T/bannertool" makebanner -i meta/banner.png -a meta/banner.wav -o "$OUT/banner.bnr" >/dev/null
"$T/bannertool" makesmdh -s "forge.store" -l "forge.store · ports for Nintendo 3DS" -p "Esteban's Forge" \
  -i meta/icon.png -o "$OUT/icon.icn" >/dev/null
(cd "$D" && "$T/makerom" -f cia -o "$OUT/forge-store.cia" -elf "$OUT/forge-store.elf" -rsf forge-store.rsf \
  -icon "$OUT/icon.icn" -banner "$OUT/banner.bnr" -target t)
python3 "$D/tools/build-manifest.py"
ls -la "$OUT/forge-store.3dsx" "$OUT/forge-store.cia"
