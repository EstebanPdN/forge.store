#!/bin/zsh
# Pinned, offline decoder build. No encoder, SIMD, threads or runtime dependency.
set -eu
tool_dir="${0:A:h}"
native_dir="${tool_dir:h}"
sdk_root="${DEVKITPRO:-$HOME/devkitpro}"
compiler_root="${DEVKITARM:-$sdk_root/devkitARM}"
cmake -S "$native_dir/vendor/libwebp-1.6.0" -B "$native_dir/build/webp" \
  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=arm \
  -DCMAKE_C_COMPILER="$compiler_root/bin/arm-none-eabi-gcc" \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  '-DCMAKE_C_FLAGS=-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations -ffunction-sections -fdata-sections' \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DWEBP_ENABLE_SIMD=OFF -DWEBP_USE_THREAD=OFF -DWEBP_LINK_STATIC=ON \
  -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
  -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
  -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF \
  -DWEBP_BUILD_LIBWEBPMUX=OFF -DWEBP_BUILD_WEBP_JS=OFF
cmake --build "$native_dir/build/webp" --target webpdecoder --parallel 4
