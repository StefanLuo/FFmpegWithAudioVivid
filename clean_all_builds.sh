#!/bin/bash

# FFmpeg AVS 项目全量清理脚本
#
# 功能：
#   清除：
#     AVS1/2/3
#     Audio Vivid / AVS3A
#     FFmpeg
#     libplacebo
#     Shaderc
#     mpv
#     libplayer / libmpv-android
#     以及所有新增第三方库的编译中间件与产物
#
# 注意：
#   - 只清理编译产物和中间文件
#   - 不删除源码
#   - 不删除 git 仓库
#   - 不删除配置脚本本身
#

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo
echo "======================================================="
echo "=== FFmpeg AVS 项目全量清理 ==="
echo "======================================================="
echo
echo "ROOT_DIR:"
echo "  $ROOT_DIR"
echo


# ============================================================
# [1/11] FFmpeg
# ============================================================

echo "=== [1/11] 正在清理 FFmpeg_n9.0.1 ==="

if [ -d "$ROOT_DIR/FFmpeg_n9.0.1" ]; then

    cd "$ROOT_DIR/FFmpeg_n9.0.1" || exit 1

    make distclean >/dev/null 2>&1 || true

    cd "$ROOT_DIR" || exit 1

fi

echo


# ============================================================
# [2/11] davs2 / xavs2
# ============================================================

echo "=== [2/11] 正在清理 libdavs2 & libxavs2 ==="

# ------------------------------------------------------------
# davs2
# ------------------------------------------------------------

if [ -d "$ROOT_DIR/davs2" ]; then

    echo ">>> Cleaning davs2..."

    rm -f \
        "$ROOT_DIR"/davs2/*.a

    rm -rf \
        "$ROOT_DIR"/davs2/build/android_*

    if [ -d "$ROOT_DIR/davs2/build/linux" ]; then

        cd "$ROOT_DIR/davs2/build/linux" || exit 1

        make distclean >/dev/null 2>&1 || true

        rm -f \
            config.h \
            config.log \
            config.mak \
            davs2.pc \
            conftest.c \
            conftest.log

        rm -rf \
            common \
            test

        cd "$ROOT_DIR" || exit 1

    fi

fi


# ------------------------------------------------------------
# xavs2
# ------------------------------------------------------------

if [ -d "$ROOT_DIR/xavs2" ]; then

    echo ">>> Cleaning xavs2..."

    rm -f \
        "$ROOT_DIR"/xavs2/*.a

    rm -rf \
        "$ROOT_DIR"/xavs2/build/android_*

    if [ -d "$ROOT_DIR/xavs2/build/linux" ]; then

        cd "$ROOT_DIR/xavs2/build/linux" || exit 1

        make distclean >/dev/null 2>&1 || true

        rm -f \
            config.h \
            config.log \
            config.mak \
            xavs2.pc \
            conftest.c \
            conftest.log

        rm -rf \
            common \
            encoder \
            test

        cd "$ROOT_DIR" || exit 1

    fi

fi

echo


# ============================================================
# [3/11] uavs3d / uavs3e
# ============================================================

echo "=== [3/11] 正在清理 libuavs3d & libuavs3e ==="

if [ -d "$ROOT_DIR/uavs3d" ]; then

    echo ">>> Cleaning uavs3d..."

    rm -rf \
        "$ROOT_DIR"/uavs3d/build_*

    rm -f \
        "$ROOT_DIR"/uavs3d/uavs3d.pc

fi

if [ -d "$ROOT_DIR/uavs3e" ]; then

    echo ">>> Cleaning uavs3e..."

    rm -rf \
        "$ROOT_DIR"/uavs3e/build_*

    rm -f \
        "$ROOT_DIR"/uavs3e/uavs3e.pc

fi

echo


# ============================================================
# [4/11] Meson projects
# ============================================================

echo "=== [4/11] 正在清理 libdav1d, fribidi, harfbuzz ==="

for MESON_DIR in \
    "dav1d" \
    "fribidi" \
    "harfbuzz"
do

    if [ -d "$ROOT_DIR/$MESON_DIR" ]; then

        echo ">>> Cleaning $MESON_DIR..."

        rm -rf \
            "$ROOT_DIR/$MESON_DIR/build_android"

        rm -rf \
            "$ROOT_DIR/$MESON_DIR"/build_*

        rm -rf \
            "$ROOT_DIR/$MESON_DIR/.mesonpy-"*

        rm -rf \
            "$ROOT_DIR/$MESON_DIR/.ninja_"*

    fi

done

echo


# ============================================================
# [5/11] libplacebo
# ============================================================

echo "=== [5/11] 正在清理 libplacebo ==="

if [ -d "$ROOT_DIR/libplacebo" ]; then

    echo ">>> Cleaning libplacebo..."

    # 可能存在的本地 Meson 构建目录
    rm -rf \
        "$ROOT_DIR/libplacebo/build_android"

    rm -rf \
        "$ROOT_DIR/libplacebo"/build_*

    rm -rf \
        "$ROOT_DIR/libplacebo/.mesonpy-"*

    rm -rf \
        "$ROOT_DIR/libplacebo/.ninja_"*

fi

echo


# ============================================================
# [6/11] mpv
# ============================================================

echo "=== [6/11] 正在清理 mpv ==="

if [ -d "$ROOT_DIR/mpv" ]; then

    echo ">>> Cleaning mpv..."

    # 可能存在的本地 Meson 构建目录
    rm -rf \
        "$ROOT_DIR/mpv/build_android"

    rm -rf \
        "$ROOT_DIR/mpv"/build_*

    rm -rf \
        "$ROOT_DIR/mpv/.mesonpy-"*

    rm -rf \
        "$ROOT_DIR/mpv/.ninja_"*

fi

echo


# ============================================================
# [7/11] CMake projects
# ============================================================

echo "=== [7/11] 正在清理 freetype, libxml2, ogg, vorbis, libwebp, shaderc ==="

for CMAKE_DIR in \
    "freetype" \
    "libxml2" \
    "ogg" \
    "vorbis" \
    "libwebp" \
    "shaderc"
do

    if [ -d "$ROOT_DIR/$CMAKE_DIR" ]; then

        echo ">>> Cleaning $CMAKE_DIR..."

        rm -rf \
            "$ROOT_DIR/$CMAKE_DIR/build_android"

        rm -rf \
            "$ROOT_DIR/$CMAKE_DIR/build_msys"

        rm -rf \
            "$ROOT_DIR/$CMAKE_DIR"/build_*

        rm -rf \
            "$ROOT_DIR/$CMAKE_DIR/.cxx"

    fi

done

echo


# ============================================================
# [8/11] OpenSSL / Opus
# ============================================================

echo "=== [8/11] 正在清理 openssl & libopus ==="

# ------------------------------------------------------------
# OpenSSL
# ------------------------------------------------------------

if [ -d "$ROOT_DIR/openssl" ]; then

    echo ">>> Cleaning OpenSSL..."

    cd "$ROOT_DIR/openssl" || exit 1

    make clean >/dev/null 2>&1 || true

    rm -f \
        Makefile \
        config.log

    cd "$ROOT_DIR" || exit 1

fi


# ------------------------------------------------------------
# Opus
# ------------------------------------------------------------

if [ -d "$ROOT_DIR/opus" ]; then

    echo ">>> Cleaning Opus..."

    cd "$ROOT_DIR/opus" || exit 1

    make distclean >/dev/null 2>&1 || true

    rm -rf \
        autom4te.cache \
        config.status \
        config.log \
        Makefile

    cd "$ROOT_DIR" || exit 1

fi

echo


# ============================================================
# [9/11] libass
# ============================================================

echo "=== [9/11] 正在清理 libass ==="

if [ -d "$ROOT_DIR/libass" ]; then

    echo ">>> Cleaning libass..."

    cd "$ROOT_DIR/libass" || exit 1

    make distclean >/dev/null 2>&1 || true

    rm -rf \
        autom4te.cache \
        config.status \
        config.log \
        Makefile \
        configure

    cd "$ROOT_DIR" || exit 1

fi

echo


# ============================================================
# [10/11] libmpv-android / libplayer
# ============================================================

echo "=== [10/11] 正在清理 libmpv-android / libplayer ==="

if [ -d "$ROOT_DIR/libmpv-android" ]; then

    echo
    echo ">>> Cleaning libmpv-android build artifacts..."

    # --------------------------------------------------------
    # Gradle
    # --------------------------------------------------------

    rm -rf \
        "$ROOT_DIR/libmpv-android/.gradle"

    # --------------------------------------------------------
    # Android Studio / CMake native build
    # --------------------------------------------------------

    rm -rf \
        "$ROOT_DIR/libmpv-android/.cxx"

    # --------------------------------------------------------
    # libmpv module build output
    # --------------------------------------------------------

    if [ -d "$ROOT_DIR/libmpv-android/libmpv" ]; then

        rm -rf \
            "$ROOT_DIR/libmpv-android/libmpv/build"

        rm -rf \
            "$ROOT_DIR/libmpv-android/libmpv/.cxx"

    fi

    # --------------------------------------------------------
    # 任何意外产生的 libplayer.so
    #
    # 只删除二进制，不删除源码。
    # --------------------------------------------------------

    find "$ROOT_DIR/libmpv-android" \
        -type f \
        -name "libplayer.so" \
        -delete \
        >/dev/null 2>&1 || true

    # --------------------------------------------------------
    # 常见 CMake 临时残留
    # --------------------------------------------------------

    find "$ROOT_DIR/libmpv-android" \
        -type d \
        \( \
            -name "CMakeFiles" \
            -o \
            -name "cmake-build-*" \
        \) \
        -prune \
        -exec rm -rf {} + \
        >/dev/null 2>&1 || true

    echo
    echo ">>> libmpv-android source tree preserved."

else

    echo ">>> libmpv-android source directory not found, skip."

fi

echo


# ============================================================
# [11/11] Media3
# ============================================================

echo "=== [11/11] 正在清理 Media3 Android 项目 ==="

if [ -d "$ROOT_DIR/media3" ]; then

    cd "$ROOT_DIR/media3" || exit 1

    if [ -f "./gradlew" ]; then

        chmod +x ./gradlew

        ./gradlew clean >/dev/null 2>&1 || true

    fi

    find . \
        -type d \
        -name "buildout" \
        -exec rm -rf {} + \
        >/dev/null 2>&1 || true

    find . \
        -type d \
        -name ".gradle" \
        -exec rm -rf {} + \
        >/dev/null 2>&1 || true

    find . \
        -type d \
        -name ".cxx" \
        -exec rm -rf {} + \
        >/dev/null 2>&1 || true

    find . \
        -type d \
        -name ".externalNativeBuild" \
        -exec rm -rf {} + \
        >/dev/null 2>&1 || true

    cd "$ROOT_DIR" || exit 1

fi

echo


# ============================================================
# Final global cleanup
# ============================================================

echo
echo "======================================================="
echo "=== [最终清洗] 移除所有 Android 编译输出及中间残余 ==="
echo "======================================================="
echo


# ------------------------------------------------------------
# FFmpeg / MPV 最终输出
# ------------------------------------------------------------

echo ">>> Removing android_build..."
rm -rf \
    "$ROOT_DIR/android_build"


# ------------------------------------------------------------
# Shaderc / libplacebo / libplayer / third-party build
# ------------------------------------------------------------

echo ">>> Removing android_build_deps..."
rm -rf \
    "$ROOT_DIR/android_build_deps"


# ------------------------------------------------------------
# Final dependency installation
# ------------------------------------------------------------

echo ">>> Removing android_deps..."
rm -rf \
    "$ROOT_DIR/android_deps"


# ------------------------------------------------------------
# Other generic build roots
# ------------------------------------------------------------

echo ">>> Removing *_build directories..."
rm -rf \
    "$ROOT_DIR"/*_build


# ------------------------------------------------------------
# Temporary files
# ------------------------------------------------------------

echo ">>> Removing temporary files..."

rm -f \
    "$ROOT_DIR"/*.tmp \
    "$ROOT_DIR"/log_dec.txt


# ============================================================
# Final status
# ============================================================

echo
echo "已清理："
echo
echo "  FFmpeg n9.0.1 编译产物"
echo "  davs2 / xavs2 编译产物"
echo "  uavs3d / uavs3e 编译产物"
echo "  dav1d / fribidi / harfbuzz 编译产物"
echo "  libplacebo 编译产物"
echo "  Shaderc 编译产物"
echo "  mpv 编译产物"
echo "  libmpv 编译产物"
echo "  libplayer 编译产物"
echo "  libmpv-android Gradle/CMake 编译产物"
echo "  freetype / libxml2 / ogg / vorbis / libwebp 编译产物"
echo "  openssl / opus / libass 编译产物"
echo "  Media3 Android 编译产物"
echo
echo "  android_build"
echo "  android_build_deps"
echo "  android_deps"
echo
echo "======================================================="
echo ">>> 全量清理完成"
echo "======================================================="
echo