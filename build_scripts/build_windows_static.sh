#!/bin/bash

# FFmpeg Windows 极致静态巅峰版 (V10 - 功能性能全对齐 + 智能部署完全体)
source ./build_scripts/env.sh

PREFIX=$(readlink -f "$WINDOWS_BUILD_DIR")_static
echo "Building FFmpeg Windows Static - THE ULTIMATE FLAGSHIP"

# 1. 强力清理进程
taskkill.exe /F /IM ffmpeg.exe /T 2>/dev/null
taskkill.exe /F /IM ffplay.exe /T 2>/dev/null
taskkill.exe /F /IM ffprobe.exe /T 2>/dev/null
sleep 1

# 重置输出目录
rm -rf "$PREFIX" 2>/dev/null
mkdir -p "$PREFIX/bin"

export PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:/mingw64/share/pkgconfig:$PKG_CONFIG_PATH"
export PKG_CONFIG="pkg-config --static"

make distclean 2>/dev/null

# 2. 旗舰静态全功能配置 (与 Shared 版功能完全对齐)
cfg_options=(
    --prefix="$PREFIX"
    --target-os=mingw32
    --arch=x86_64
    --cpu=native
    --enable-static
    --disable-shared
    --enable-w32threads
    --enable-network
    --enable-openssl
    --pkg-config-flags="--static"
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
    --enable-libass
    --enable-libxml2
    --enable-libdav1d
    --enable-libaom
    --enable-libsvtav1
    --enable-libmysofa
    --enable-libwebp
    --enable-libopenjpeg
    --enable-libjxl
    --enable-libsrt
    --enable-librtmp
    --enable-libssh
    --enable-libvidstab
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
    --enable-libbluray
    --enable-libsnappy
    --enable-ffmpeg
    --enable-ffprobe
    --enable-ffplay
    --ld="g++"
    --extra-cflags="-static $OPT_FLAGS -mavx2 -msse4.2"
    --extra-ldflags="-static-libgcc -static-libstdc++ -Wl,--allow-multiple-definition"
)

# 合并公共标志
for flag in $COMMON_FF_CFG_FLAGS; do
    cfg_options+=("$flag")
done

./configure "${cfg_options[@]}"
make -j$(nproc 2>/dev/null || echo 4)
make install

# 3. 整理部署 (即使是静态版，某些系统驱动和底层 DLL 也是运行必需的)
echo "Deploying Artifacts and scanning for persistent DLL dependencies..."
mv -f *.exe "$PREFIX/bin/" 2>/dev/null
cp -vf model.bin "$PREFIX/bin/" 2>/dev/null

MSYS_BIN="/mingw64/bin"
EXES=("$PREFIX/bin/ffmpeg.exe" "$PREFIX/bin/ffplay.exe" "$PREFIX/bin/ffprobe.exe")

# A. 使用 ldd 自动扫描 (确保带走任何未静态化的系统层依赖)
for exe in "${EXES[@]}"; do
    if [ -f "$exe" ]; then
        echo "Scanning $exe..."
        dependencies=$(ldd "$exe" | grep '/mingw64/bin/' | awk '{print $3}')
        for dll in $dependencies; do
            cp -vn "$dll" "$PREFIX/bin/" 2>/dev/null
        done
    fi
done

# B. 强制补齐核心加速与底层运行时 DLL
extra_dlls=(
    "vulkan-1.dll" "libvulkan-1.dll" "libshaderc_shared-*.dll"
    "libspirv-cross-c-shared.dll" "libplacebo-*.dll" "libdavs2-*.dll"
    "libuavs3d.dll" "libxavs2-*.dll" "libass-*.dll" "libxml2-*.dll"
    "liblcms2-*.dll" "libbrotli*.dll" "libva*.dll" "libgomp-*.dll"
    "libwinpthread-*.dll" "libstdc++-*.dll" "libgcc_s_seh-*.dll"
    "libmysofa*.dll" "libsrt*.dll" "libssh*.dll" "libdav1d*.dll" "libjxl*.dll"
    "libmp3lame-*.dll" "libopus-*.dll" "libvorbis-*.dll" "libvpx-*.dll" "libzimg-*.dll"
)
for p in "${extra_dlls[@]}"; do
    cp -vf $MSYS_BIN/$p "$PREFIX/bin/" 2>/dev/null
done

# 清理无关产物目录
rm -rf "$PREFIX/lib" "$PREFIX/include" "$PREFIX/share"

echo "-------------------------------------------------------"
echo "WINDOWS STATIC BUILD & COMPLETE DEPLOY SUCCESSFUL!"
echo "-------------------------------------------------------"
