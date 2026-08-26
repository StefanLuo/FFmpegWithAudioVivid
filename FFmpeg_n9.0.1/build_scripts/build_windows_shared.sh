#!/bin/bash

source ./build_scripts/env.sh

PREFIX=$(readlink -f "$WINDOWS_BUILD_DIR")_shared
echo "Building FFmpeg Shared Libraries (DLLs) - VULKAN, AVS+, AVS3 (FULL)"

# --- 1. 强力进程清理 ---
echo "Cleaning up processes..."
taskkill.exe /F /IM ffmpeg.exe /T 2>/dev/null
taskkill.exe /F /IM ffplay.exe /T 2>/dev/null
taskkill.exe /F /IM ffprobe.exe /T 2>/dev/null
sleep 2

# 强制解锁
if [ -d "$PREFIX" ]; then
    mv "$PREFIX" "${PREFIX}_old_$(date +%s)" 2>/dev/null || true
fi
rm -rf "$PREFIX" 2>/dev/null
mkdir -p "$PREFIX/bin" "$PREFIX/lib" "$PREFIX/include"

export PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:/mingw64/share/pkgconfig:$PKG_CONFIG_PATH"

# 修复 configure 探测 Bug
if [ -f "configure" ]; then
    sed -i 's/require libplacebo libplacebo.h/require libplacebo libplacebo\/config.h/g' configure
fi

make distclean 2>/dev/null

# --- 2. 动态库配置 ---
cfg_options=(
    --prefix="$PREFIX"
    --target-os=mingw32
    --arch=x86_64
    --disable-static
    --enable-shared
    --enable-w32threads
    --enable-hwaccels
    --enable-dxva2
    --enable-d3d11va
    --enable-cuda-llvm
    --enable-cuvid
    --enable-nvenc
    --enable-nvdec
    --enable-libvpl
    --enable-amf
    --enable-vulkan
    --enable-libplacebo
    --enable-libx264
    --enable-libx265
    --enable-libdavs2
    --enable-libuavs3d
    --enable-libxavs2
    --enable-libmp3lame
    --enable-libopus
    --enable-libvorbis
    --enable-libvpx
    --enable-libzimg
    --enable-libsoxr
    --enable-librubberband
    --enable-libxml2
    --enable-libbluray
    --enable-libsnappy
    --enable-openssl
    --enable-ffmpeg
    --enable-ffprobe
    --enable-ffplay
    --extra-ldflags="-Wl,--allow-multiple-definition"
)

for flag in $COMMON_FF_CFG_FLAGS; do
    cfg_options+=("$flag")
done

./configure "${cfg_options[@]}"
make -j$(nproc 2>/dev/null || echo 4)
make install

# --- 3. 部署资产 ---
echo "Deploying Artifacts and AVS3 Models..."
mv -f "$PREFIX/lib/"*.dll "$PREFIX/bin/" 2>/dev/null
cp -vf model.bin "$PREFIX/bin/" 2>/dev/null

echo "Smart Scanning for 64-bit dependencies via ldd..."
MSYS_BIN="/mingw64/bin"
EXES=("$PREFIX/bin/ffmpeg.exe" "$PREFIX/bin/ffplay.exe" "$PREFIX/bin/ffprobe.exe")

for exe in "${EXES[@]}"; do
    if [ -f "$exe" ]; then
        dependencies=$(ldd "$exe" | grep '/mingw64/bin/' | awk '{print $3}')
        for dll in $dependencies; do
            cp -vn "$dll" "$PREFIX/bin/" 2>/dev/null
        done
    fi
done

# 补齐 Vulkan 与 AVS 系列运行时必备 DLL
extra_dlls=(
    "vulkan-1.dll" "libvulkan-1.dll" "libshaderc_shared-*.dll"
    "libspirv-cross-c-shared.dll" "libplacebo-*.dll" "libdavs2-*.dll"
    "libuavs3d.dll" "libxavs2-*.dll"
    "liblcms2-*.dll" "libbrotli*.dll" "libva*.dll" "libgomp-*.dll"
)
for p in "${extra_dlls[@]}"; do
    cp -vf $MSYS_BIN/$p "$PREFIX/bin/" 2>/dev/null
done

# 清理
rm -f *.exe *.dll *.a *.lib

echo "-------------------------------------------------------"
echo "FULL SHARED BUILD SUCCESSFUL (AVS+, AVS3, VULKAN)!"
echo "-------------------------------------------------------"
