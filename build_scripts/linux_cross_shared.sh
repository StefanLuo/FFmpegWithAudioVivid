#!/bin/bash

# FFmpeg Linux -> Windows (Shared DLLs) Direct Source Build
# This script performs pure cross-compilation without external helpers.

set -e

# --- 1. Source Environment ---
source ./build_scripts/env.sh

# --- 2. Configuration ---
CROSS_PREFIX="${WINDOWS_CROSS_PREFIX:-x86_64-w64-mingw32-}"
PREFIX="${LINUX_BUILD_DIR:-$(pwd)/linux_cross_build}_shared"

echo "Starting FFmpeg Linux -> Windows Cross-Compilation (Shared)"
echo "Target Directory: $PREFIX"
echo "Using Cross Prefix: $CROSS_PREFIX"

# Clean previous build artifacts
rm -rf "$PREFIX"
mkdir -p "$PREFIX/bin"

# Patch configure for libplacebo on n9.0.1
if [ -f "configure" ]; then
    sed -i 's/require libplacebo libplacebo.h/require libplacebo libplacebo\/config.h/g' configure
fi

make distclean 2>/dev/null || true

# --- 3. Execute Configure ---
cfg_options=(
    --prefix="$PREFIX"
    --target-os=mingw32
    --arch=x86_64
    --cross-prefix="$CROSS_PREFIX"
    --enable-cross-compile
    --disable-static
    --enable-shared
    --enable-w32threads

    # Features from env.sh (or fallback to defaults if not set)
    --enable-hwaccels
    --enable-dxva2
    --enable-d3d11va
    --enable-nvenc
    --enable-nvdec

    --enable-ffmpeg
    --enable-ffprobe
    --enable-ffplay

    --extra-ldflags="-Wl,--allow-multiple-definition"
)

# Append common flags from env.sh
for flag in $COMMON_FF_CFG_FLAGS; do
    cfg_options+=("$flag")
done

./configure "${cfg_options[@]}"

# --- 4. Build & Install ---
echo "Building with $(nproc 2>/dev/null || echo 4) cores..."
make -j$(nproc 2>/dev/null || echo 4)
make install

# --- 5. Assets Deployment ---
echo "Deploying AVS3 specific assets..."
if [ -f "model.bin" ]; then
    cp -vf model.bin "$PREFIX/bin/"
fi

# In cross-build, DLLs are often placed in bin already, but check lib just in case
mv -f "$PREFIX/lib/"*.dll "$PREFIX/bin/" 2>/dev/null || true

echo "-------------------------------------------------------"
echo "CROSS-COMPILATION COMPLETE!"
echo "Binaries & DLLs: $PREFIX/bin"
echo "-------------------------------------------------------"
