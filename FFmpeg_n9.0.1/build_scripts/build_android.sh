#!/bin/bash

source ./build_scripts/env.sh

if [ ! -d "$NDK_PATH" ]; then
    echo "Error: NDK_PATH is not set correctly in env.sh"
    exit 1
fi

# Determine host OS for NDK toolchain
OS_NAME=$(uname -s | tr '[:upper:]' '[:lower:]')
case "$OS_NAME" in
    linux*)  HOST_TAG="linux-x86_64" ;;
    darwin*) HOST_TAG="darwin-x86_64" ;;
    msys*|mingw*) HOST_TAG="windows-x86_64" ;;
    *) echo "Unsupported host OS: $OS_NAME"; exit 1 ;;
esac

TOOLCHAIN=$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG

build_one() {
    ARCH=$1
    CPU=$2
    CROSS_PREFIX=$3
    TARGET=$4

    echo "Building for $ARCH..."

    PREFIX=$BUILD_DIR_ROOT/$ARCH
    mkdir -p $PREFIX

    ./configure \
        --prefix=$PREFIX \
        --target-os=android \
        --arch=$ARCH \
        --cpu=$CPU \
        --cc=$TOOLCHAIN/bin/${TARGET}${API_LEVEL}-clang \
        --cxx=$TOOLCHAIN/bin/${TARGET}${API_LEVEL}-clang++ \
        --cross-prefix=$TOOLCHAIN/bin/$CROSS_PREFIX \
        --nm=$TOOLCHAIN/bin/llvm-nm \
        --strip=$TOOLCHAIN/bin/llvm-strip \
        --enable-shared \
        --disable-static \
        --enable-cross-compile \
        --enable-mediacodec \
        --enable-jni \
        --enable-hwaccel=h264_mediacodec \
        --enable-hwaccel=hevc_mediacodec \
        --enable-hwaccel=mpeg4_mediacodec \
        --enable-hwaccel=vp8_mediacodec \
        --enable-hwaccel=vp9_mediacodec \
        $COMMON_FF_CFG_FLAGS

    make clean
    make -j$(nproc 2>/dev/null || echo 4)
    make install
}

# Build for major architectures
# build_one "arm64-v8a" "armv8-a" "aarch64-linux-android-" "aarch64-linux-android"
# build_one "armeabi-v7a" "armv7-a" "arm-linux-androideabi-" "armv7a-linux-androideabi"

echo "Android build script prepared. Please uncomment the architectures you want to build in build_android.sh"
