#!/bin/bash

source ./build_scripts/env.sh

PREFIX=$(readlink -f "$WINDOWS_BUILD_DIR")_static
echo "Building 'Single-EXE-Ghost' FFmpeg Tools (FULL AVS & VULKAN) - STATIC"

# --- 1. 强力进程清理 ---
echo "Cleaning up processes..."
taskkill.exe /F /IM ffmpeg.exe /T 2>/dev/null
taskkill.exe /F /IM ffplay.exe /T 2>/dev/null
taskkill.exe /F /IM ffprobe.exe /T 2>/dev/null
sleep 2

if [ -d "$PREFIX" ]; then
    mv "$PREFIX" "${PREFIX}_old_$(date +%s)" 2>/dev/null || true
fi
rm -rf "$PREFIX" 2>/dev/null
mkdir -p "$PREFIX/bin"

export PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:/mingw64/share/pkgconfig:$PKG_CONFIG_PATH"
export PKG_CONFIG="pkg-config --static"

# 修复 configure 探测 Bug
if [ -f "configure" ]; then
    sed -i 's/require libplacebo libplacebo.h/require libplacebo libplacebo\/config.h/g' configure
fi

make distclean 2>/dev/null

# --- 2. 极致静态配置 (加入 AVS3 视频解码) ---
cfg_options=(
    --prefix="$PREFIX"
    --target-os=mingw32
    --arch=x86_64
    --enable-static
    --disable-shared
    --enable-w32threads
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
    --ld="g++"
    --extra-cflags="-static -DPL_STATIC"
    --extra-ldflags="-static-libgcc -static-libstdc++ -Wl,--allow-multiple-definition"
    # 【核心】：显式链接 uavs3d 和 davs2
    --extra-libs="-lvulkan-1 -ldavs2 -luavs3d -lxavs2 -lplacebo -lshaderc_combined -lglslang -lMachineIndependent -lGenericCodeGen -lOSDependent -lSPIRV -lSPIRV-Tools-opt -lSPIRV-Tools -lglslang-default-resource-limits -lspirv-cross-c -lspirv-cross-glsl -lspirv-cross-hlsl -lspirv-cross-msl -lspirv-cross-reflect -lspirv-cross-util -lspirv-cross-core -llcms2 -ldovi -lbluray -lxml2 -lgnutls -lhogweed -lnettle -lgmp -lharfbuzz -lfreetype -lpng -lbz2 -lz -lsnappy -lsoxr -lrubberband -lstdc++ -lbcrypt -lcrypt32 -lole32 -luuid -lshlwapi -lgdi32 -lws2_32 -luserenv -lntdll -lwinmm -lsetupapi -limm32 -lversion"
)

for flag in $COMMON_FF_CFG_FLAGS; do
    cfg_options+=("$flag")
done

./configure "${cfg_options[@]}"
make -j$(nproc 2>/dev/null || echo 4)
make install

# --- 3. 部署与扫描 ---
mv -f *.exe "$PREFIX/bin/" 2>/dev/null
cp -vf model.bin "$PREFIX/bin/" 2>/dev/null

echo "Smart Scanning dependencies..."
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

final_safety=(
    "vulkan-1.dll" "libvulkan-1.dll" "libshaderc_shared-*.dll"
    "libspirv-cross-c-shared.dll" "libplacebo-*.dll" "libdavs2-*.dll"
    "libuavs3d.dll" "libgomp-1.dll" "libva.dll" "libva_win32.dll"
)
for p in "${final_safety[@]}"; do
    cp -vf $MSYS_BIN/$p "$PREFIX/bin/" 2>/dev/null
done

rm -rf "$PREFIX/lib" "$PREFIX/include" "$PREFIX/share"
rm -f *.dll *.a *.lib

echo "-------------------------------------------------------"
echo "FULL STATIC BUILD SUCCESSFUL (AVS+, AVS3, VULKAN)!"
echo "-------------------------------------------------------"
