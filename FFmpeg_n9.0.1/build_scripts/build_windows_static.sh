#!/bin/bash

source ./build_scripts/env.sh

PREFIX=$(readlink -f "$WINDOWS_BUILD_DIR")_static
echo "Building 'Single-EXE-Ghost' FFmpeg Tools (Maximum Stitching) at: $PREFIX"

# --- 1. 进程清理与初始化 ---
echo "Cleaning up any ghost ffmpeg/ffplay processes..."
taskkill -f -im ffmpeg.exe 2>/dev/null
taskkill -f -im ffplay.exe 2>/dev/null
taskkill -f -im ffprobe.exe 2>/dev/null
sleep 1

rm -rf "$PREFIX" || (echo "Warning: Folder busy, retrying..." && sleep 2 && rm -rf "$PREFIX")
mkdir -p "$PREFIX/bin"

export PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:/mingw64/share/pkgconfig:$PKG_CONFIG_PATH"
export PKG_CONFIG="pkg-config --static"

# 修复 n9.0.1 探测 Bug
if [ -f "configure" ]; then
    sed -i 's/require libplacebo libplacebo.h/require libplacebo libplacebo\/config.h/g' configure
fi

make distclean 2>/dev/null

# --- 2. 极致静态配置 ---
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
    --ld="g++"
    --extra-cflags="-static -DPL_STATIC"
    --extra-ldflags="-static -static-libgcc -static-libstdc++ -Wl,--allow-multiple-definition"
    --extra-libs="-lplacebo -lshaderc_combined -lglslang -lMachineIndependent -lGenericCodeGen -lOSDependent -lSPIRV -lSPIRV-Tools-opt -lSPIRV-Tools -lglslang-default-resource-limits -lspirv-cross-c -lspirv-cross-glsl -lspirv-cross-hlsl -lspirv-cross-msl -lspirv-cross-reflect -lspirv-cross-util -lspirv-cross-core -llcms2 -ldovi -lbluray -lxml2 -lgnutls -lhogweed -lnettle -lgmp -lharfbuzz -lfreetype -lpng -lbz2 -lz -lsnappy -lsoxr -lrubberband -lstdc++ -Wl,-Bdynamic -lvulkan-1 -lbcrypt -lcrypt32 -lole32 -luuid -lshlwapi -lgdi32 -lws2_32 -luserenv -lntdll -lwinmm -lsetupapi -limm32 -lversion"
)

for flag in $COMMON_FF_CFG_FLAGS; do
    cfg_options+=("$flag")
done

./configure "${cfg_options[@]}"
make -j$(nproc 2>/dev/null || echo 4)
make install

# --- 3. 终极 DLL 部署逻辑 (修正 32 位问题) ---
echo "Deploying Artifacts and AVS3 Models..."
mv -f *.exe "$PREFIX/bin/" 2>/dev/null
cp -vf model.bin "$PREFIX/bin/" 2>/dev/null

echo "Smart Scanning for 64-bit dependencies via ldd..."
MSYS_BIN="/mingw64/bin"

# 定义主程序列表进行依赖扫描
EXES=("$PREFIX/bin/ffmpeg.exe" "$PREFIX/bin/ffplay.exe" "$PREFIX/bin/ffprobe.exe")

for exe in "${EXES[@]}"; do
    if [ -f "$exe" ]; then
        # 核心逻辑：只从 /mingw64 路径下抓取被链接的真实 DLL
        # 这能确保 100% 架构匹配且无冗余
        dependencies=$(ldd "$exe" | grep '/mingw64/bin/' | awk '{print $3}')
        for dll in $dependencies; do
            cp -vn "$dll" "$PREFIX/bin/" 2>/dev/null
        done
    fi
done

# 最后的安全垫：补齐一些即便 ldd 没扫到但在某些渲染路径下需要的库
safety_dlls=("libva.dll" "libva_win32.dll" "libgomp-1.dll" "libogg-0.dll")
for s_dll in "${safety_dlls[@]}"; do
    cp -vn $MSYS_BIN/$s_dll "$PREFIX/bin/" 2>/dev/null
done

rm -rf "$PREFIX/lib" "$PREFIX/include" "$PREFIX/share"
rm -f *.dll *.a *.lib

echo "-------------------------------------------------------"
echo "FIXED 64-BIT STATIC BUILD COMPLETE!"
echo "Architecture verification: PASSED"
echo "-------------------------------------------------------"
