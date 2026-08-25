#!/bin/bash

source ./build_scripts/env.sh

PREFIX=$(readlink -f "$WINDOWS_BUILD_DIR")_shared
echo "Building FFmpeg Shared Libraries (DLLs) with AVS3 Support at: $PREFIX"

# --- 1. 进程清理 ---
echo "Cleaning up any ghost ffmpeg/ffplay processes..."
taskkill -f -im ffmpeg.exe 2>/dev/null
taskkill -f -im ffplay.exe 2>/dev/null
taskkill -f -im ffprobe.exe 2>/dev/null
sleep 1

rm -rf "$PREFIX" || (echo "Warning: Folder busy, retrying..." && sleep 2 && rm -rf "$PREFIX")
mkdir -p "$PREFIX/bin" "$PREFIX/lib" "$PREFIX/include"

export PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:/mingw64/share/pkgconfig:$PKG_CONFIG_PATH"
OS_NAME=$(uname -s | tr '[:upper:]' '[:lower:]')

# 修复 n9.0.1 configure 探测 Bug
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
    # 彻底移除 Vulkan 和 libplacebo 以解决兼容性报错
    # --enable-vulkan
    # --enable-libplacebo
    --enable-libx264
    --enable-libx265
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

# --- 3. 终极自动化部署 (智能 LDD 扫描) ---
echo "Deploying Artifacts and AVS3 Models..."
mv -f "$PREFIX/lib/"*.dll "$PREFIX/bin/" 2>/dev/null
cp -vf model.bin "$PREFIX/bin/" 2>/dev/null

echo "Collecting Import Libraries..."
find . -maxdepth 2 -name "*.dll.a" ! -path "./windows_build/*" -exec cp -f {} "$PREFIX/lib/" \;

echo "Smart Scanning for 64-bit dependencies via ldd..."
MSYS_BIN="/mingw64/bin"
EXES=("$PREFIX/bin/ffmpeg.exe" "$PREFIX/bin/ffplay.exe" "$PREFIX/bin/ffprobe.exe")

for exe in "${EXES[@]}"; do
    if [ -f "$exe" ]; then
        # 提取真实被链接的 64 位 DLL 路径
        # 增加逻辑：必须排除 vulkan-1.dll，该库必须使用系统驱动自带的版本
        dependencies=$(ldd "$exe" | grep '/mingw64/bin/' | grep -v 'vulkan-1.dll' | awk '{print $3}')
        for dll in $dependencies; do
            cp -vn "$dll" "$PREFIX/bin/" 2>/dev/null
        done
    fi
done

# 补齐即便没被显式链接但渲染路径需要的安全 DLL
safety_dlls=("libva.dll" "libva_win32.dll" "libgomp-1.dll" "libogg-0.dll" "liblcms2-2.dll")
for s_dll in "${safety_dlls[@]}"; do
    cp -vn $MSYS_BIN/$s_dll "$PREFIX/bin/" 2>/dev/null
done

# 清理不必要的中间文件
rm -f *.exe *.dll *.a *.lib

echo "-------------------------------------------------------"
echo "FIXED 64-BIT SHARED BUILD COMPLETE!"
echo "-------------------------------------------------------"
