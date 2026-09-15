#!/bin/bash

# ============================================================
# FFmpeg Android / Windows Build Environment
# MSYS2 MINGW64
# ============================================================

# 当前 env.sh 所在目录
ENV_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# FFmpegProject 根目录
PROJECT_ROOT="$(cd "$ENV_DIR/.." && pwd)"

# FFmpeg 9.0.1
FFMPEG_DIR="$PROJECT_ROOT/FFmpeg_n9.0.1"

# ============================================================
# 1. Android
# ============================================================

# Android NDK
export NDK_PATH="/d/Android/SDK/ndk/30.0.16138531"

# Android API
export API_LEVEL=26

# Android 编译输出
export BUILD_DIR_ROOT="$PROJECT_ROOT/android_build"

# Android 第三方依赖安装目录
export ANDROID_DEPS_ROOT="$PROJECT_ROOT/android_deps"

# ============================================================
# 2. Windows / MSYS2
# ============================================================

# MSYS2 根目录
export MSYS2_PATH="/d/msys64"

# Windows 编译输出
export WINDOWS_BUILD_DIR="$PROJECT_ROOT/windows_build"

# MinGW64 cross prefix
export WINDOWS_CROSS_PREFIX="x86_64-w64-mingw32-"

# ============================================================
# 3. 编译优化
# ============================================================

export OPT_FLAGS="-O3"

# ============================================================
# 4. Display
# ============================================================

echo
echo "======================================================="
echo "Build Environment"
echo "======================================================="
echo

echo ">>> General"
echo "PROJECT_ROOT              = $PROJECT_ROOT"
echo "FFMPEG_DIR                = $FFMPEG_DIR"

echo
echo ">>> Android"
echo "ANDROID_DEPS_ROOT         = $ANDROID_DEPS_ROOT"
echo "ANDROID_BUILD_DIR_ROOT    = $BUILD_DIR_ROOT"
echo "NDK_PATH                  = $NDK_PATH"
echo "API_LEVEL                 = $API_LEVEL"

echo
echo ">>> Windows"
echo "WINDOWS_BUILD_DIR_ROOT    = $WINDOWS_BUILD_DIR"
echo "WINDOWS_CROSS_PREFIX      = $WINDOWS_CROSS_PREFIX"
echo "MSYS2_PATH                = $MSYS2_PATH"

echo "======================================================="
echo