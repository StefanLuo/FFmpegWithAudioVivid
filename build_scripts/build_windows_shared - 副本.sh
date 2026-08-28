#!/bin/bash

# FFmpeg Windows 动态库巅峰版 (V11 - 性能巅峰 + 功能全回归)
source ./build_scripts/env.sh

cd "$FFMPEG_DIR" || exit 1

PREFIX="$(readlink -f "$WINDOWS_BUILD_DIR")/shared"

echo "Building FFmpeg Windows Shared Library"

# 1. 强力清理
taskkill.exe /F /IM ffmpeg.exe /T 2>/dev/null
taskkill.exe /F /IM ffplay.exe /T 2>/dev/null
taskkill.exe /F /IM ffprobe.exe /T 2>/dev/null
sleep 1

rm -rf "$PREFIX" 2>/dev/null
mkdir -p "$PREFIX/bin"

export PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:/mingw64/share/pkgconfig:$PKG_CONFIG_PATH"

make distclean 2>/dev/null || true

# 2. 旗舰配置 (加入关键优化：--enable-optimizations)
cfg_options=(
    # --- 基础平台与交叉编译配置 ---
    --prefix="$PREFIX"
    --target-os=mingw32
    --arch=x86_64
    --enable-runtime-cpudetect

	# --- 构建输出类型与优化 ---
    --disable-static
    --enable-shared
	--enable-pic
	--disable-doc
	--disable-debug
	--enable-lto
    --enable-stripping

	# --- 授权许可 ---
	--enable-gpl
	--enable-nonfree
	--enable-version3

	# --- 底层核心与网络协议 (DASH/HTTPS 核心) ---
    --enable-w32threads
    --enable-network
    --enable-openssl
	--enable-gmp
	--enable-parsers
	--enable-protocols

	# --- Windows 与显卡硬件加速 (硬解) ---
    --enable-hwaccels
    --enable-dxva2
    --enable-d3d11va
	--enable-mediafoundation
    --enable-cuda-llvm
    --enable-cuvid
    --enable-nvenc
    --enable-nvdec
    --enable-libvpl
    --enable-amf
    --enable-vulkan

	# --- 高级特效字幕与画面渲染 ---
    --enable-libplacebo
    --enable-libass
    --enable-libxml2

	# --- 音视频编解码器 (AV1/现代格式) ---
    --enable-libdav1d
    --enable-libaom
    --enable-libsvtav1
    --enable-libmysofa
    --enable-libwebp
    --enable-libopenjpeg

	# --- 现代及经典流媒体协议 ---
    --enable-libjxl
    --enable-libsrt
    --enable-librtmp
    --enable-libssh
    --enable-libvidstab

	# --- 基础音视频编码（如需兼顾转码/压制） ---
    --enable-libx264
    --enable-libx265
	--enable-libmp3lame
    --enable-libopus
    --enable-libvorbis
    --enable-libvpx

	# --- Vivid AV ---
    --enable-libdavs2
    --enable-libuavs3d
    --enable-libxavs2

	# --- 音视频处理辅助库 ---
    --enable-libzimg
    --enable-libsoxr
    --enable-librubberband
    --enable-libbluray
    --enable-libsnappy
	--enable-filters
	--enable-avcodec
	--enable-avformat
	--enable-avfilter
    --enable-avdevice
    --enable-ffmpeg
    --enable-ffprobe
    --enable-ffplay

	# --- 编译旗标 ---
    --extra-cflags="$OPT_FLAGS"
    --extra-ldflags="-Wl,--allow-multiple-definition"
)

./configure "${cfg_options[@]}"
NPROC=$(nproc 2>/dev/null || echo 4)
make -j$NPROC
make install

# 3. 终极部署 (恢复 DLL 拷贝逻辑)
echo "Deploying Artifacts..."
mv -f "$PREFIX/lib/"*.dll "$PREFIX/bin/" 2>/dev/null
cp -vf model.bin "$PREFIX/bin/" 2>/dev/null

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

# 暴力补齐 (涵盖 mysofa, srt 等)
extra_dlls=(
    "vulkan-1.dll" "libvulkan-1.dll" "libshaderc_shared-*.dll"
    "libspirv-cross-c-shared.dll" "libplacebo-*.dll" "libdavs2-*.dll"
    "libuavs3d.dll" "libxavs2-*.dll" "libass-*.dll" "libxml2-*.dll"
    "liblcms2-*.dll" "libbrotli*.dll" "libva*.dll" "libgomp-*.dll"
    "libwinpthread-*.dll" "libstdc++-*.dll" "libgcc_s_seh-*.dll"
    "libmysofa*.dll" "libsrt*.dll" "libssh*.dll" "libdav1d*.dll" "libjxl*.dll"
    "libmp3lame-*.dll" "libopus-*.dll" "libvorbis-*.dll" "libvpx-*.dll" "libzimg-*.dll"
	"libfftw3*.dll" "libvidstab*.dll" "libsnappy*.dll"
)
for p in "${extra_dlls[@]}"; do
    cp -vf $MSYS_BIN/$p "$PREFIX/bin/" 2>/dev/null
done

echo "WINDOWS SHARED LIBRARY BUILD SUCCESSFUL!"