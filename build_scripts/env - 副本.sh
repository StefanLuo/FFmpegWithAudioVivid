#!/bin/bash

# ============================================================
# FFmpeg Android / Windows Build Environment
# ============================================================

# 当前 env.sh 所在目录
ENV_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# FFmpegProject 根目录
PROJECT_ROOT="$(cd "$ENV_DIR/.." && pwd)"

# FFmpeg 9.0.1 源码目录
FFMPEG_DIR="$PROJECT_ROOT/FFmpeg_n9.0.1"

# ============================================================
# 1. Android Configuration
# ============================================================

export NDK_PATH="/d/Android/SDK/ndk/30.0.16138531"
export API_LEVEL=23

# Android 三架构最终输出
export BUILD_DIR_ROOT="$PROJECT_ROOT/android_build"

# Android 第三方依赖
export ANDROID_DEPS_ROOT="$PROJECT_ROOT/android_deps"

# ============================================================
# 2. Windows Configuration
# ============================================================

export MSYS2_PATH="D:/msys64"

export WINDOWS_BUILD_DIR="$PROJECT_ROOT/windows_build"

export WINDOWS_CROSS_PREFIX="x86_64-w64-mingw32-"

# ============================================================
# 3. 编译优化
# ============================================================

export OPT_FLAGS="-O3"

# ============================================================
# 4. 显示配置
# ============================================================

echo
echo "======================================================="
echo "Build Environment"
echo "======================================================="
echo "PROJECT_ROOT      = $PROJECT_ROOT"
echo "FFMPEG_DIR        = $FFMPEG_DIR"
echo "ANDROID_DEPS_ROOT = $ANDROID_DEPS_ROOT"
echo "BUILD_DIR_ROOT    = $BUILD_DIR_ROOT"
echo "NDK_PATH          = $NDK_PATH"
echo "API_LEVEL         = $API_LEVEL"
echo "======================================================="
echo