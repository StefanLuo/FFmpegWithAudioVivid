#!/bin/bash

# FFmpeg Build Environment Configuration

# --- Android Configuration ---
export NDK_PATH="/path/to/your/android-ndk"
export API_LEVEL=21

# --- General Configuration ---
export MSYS2_PATH="D:/msys64"
export BOOST_PATH="D:/local/boost_1_92_0"

export BUILD_DIR_ROOT=$(pwd)/android_build
export WINDOWS_BUILD_DIR=$(pwd)/windows_build
export LINUX_BUILD_DIR=$(pwd)/linux_build

# Common configure flags (Maximized for high performance and full features)
# Added Vulkan, HW Accels, and common formats
export COMMON_FF_CFG_FLAGS="--enable-gpl \
    --enable-nonfree \
    --enable-version3 \
    --disable-doc \
    --disable-debug \
    --enable-pic \
    --enable-runtime-cpudetect \
    --enable-avfilter \
    --enable-network \
    --enable-swresample \
    --enable-swscale \
    --enable-avcodec \
    --enable-avformat \
    --enable-avdevice"

# Toolchain prefixes
export WINDOWS_CROSS_PREFIX="x86_64-w64-mingw32-"
