#!/bin/bash

set -euo pipefail

# ============================================================
# Script / project paths
#
# build_android_deps.sh:
#   FFmpegProject/build_android_deps.sh
#
# env.sh:
#   FFmpegProject/build_scripts/env.sh
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

cd "$PROJECT_ROOT"

ENV_FILE="$PROJECT_ROOT/build_scripts/env.sh"

if [ ! -f "$ENV_FILE" ]; then
    echo "[ERROR] env.sh not found:"
    echo "        $ENV_FILE"
    exit 1
fi

source "$ENV_FILE"

echo
echo "======================================================="
echo ">>> Build environment"
echo "======================================================="
echo "    SCRIPT_DIR   = $SCRIPT_DIR"
echo "    PROJECT_ROOT = $PROJECT_ROOT"
echo "    ENV_FILE     = $ENV_FILE"
echo

# ============================================================
# Required environment
# ============================================================

: "${PROJECT_ROOT:?PROJECT_ROOT is not set}"
: "${NDK_PATH:?NDK_PATH is not set}"
: "${API_LEVEL:?API_LEVEL is not set}"
: "${ANDROID_DEPS_ROOT:?ANDROID_DEPS_ROOT is not set}"
: "${OPT_FLAGS:?OPT_FLAGS is not set}"

# ============================================================
# Source directories
# ============================================================

DAVS2_DIR="$PROJECT_ROOT/davs2"
UAVS3D_DIR="$PROJECT_ROOT/uavs3d"
OPENSSL_DIR="$PROJECT_ROOT/openssl"
DAV1D_DIR="$PROJECT_ROOT/dav1d"
LIBASS_DIR="$PROJECT_ROOT/libass"
FREETYPE_DIR="$PROJECT_ROOT/freetype"
FRIBIDI_DIR="$PROJECT_ROOT/fribidi"
HARFBUZZ_DIR="$PROJECT_ROOT/harfbuzz"
XML2_DIR="$PROJECT_ROOT/libxml2"
OPUS_DIR="$PROJECT_ROOT/opus"
OGG_DIR="$PROJECT_ROOT/ogg"
VORBIS_DIR="$PROJECT_ROOT/vorbis"
WEBP_DIR="$PROJECT_ROOT/libwebp"

# ============================================================
# Android NDK
# ============================================================

HOST_TAG="windows-x86_64"

TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG"
LLVM_BIN="$TOOLCHAIN/bin"
SYSROOT="$TOOLCHAIN/sysroot"

ANDROID_CMAKE_TOOLCHAIN="$NDK_PATH/build/cmake/android.toolchain.cmake"

# ============================================================
# Dependency directories
# ============================================================

DEPS_BUILD_ROOT="$PROJECT_ROOT/android_build_deps"
DEPS_ROOT="$ANDROID_DEPS_ROOT"

# ============================================================
# PATH
#
# MSYS2 is used for build tools.
# Android NDK LLVM tools are also available.
# ============================================================

export PATH="/mingw64/bin:/usr/local/bin:/usr/bin:/bin:$LLVM_BIN"

MESON_BIN="/mingw64/bin/meson"
NINJA_BIN="/mingw64/bin/ninja"

if [ ! -x "$MESON_BIN" ]; then
    echo "[ERROR] Meson not found: $MESON_BIN"
    exit 1
fi

if [ ! -x "$NINJA_BIN" ]; then
    echo "[ERROR] Ninja not found: $NINJA_BIN"
    exit 1
fi

# ============================================================
# ABI selector
# ============================================================

select_archs()
{
    if [ "$#" -eq 1 ]; then

        case "$1" in

            arm64-v8a)
                SELECTED_ARCHS=("arm64-v8a")
                return 0
                ;;

            armeabi-v7a)
                SELECTED_ARCHS=("armeabi-v7a")
                return 0
                ;;

            x86_64)
                SELECTED_ARCHS=("x86_64")
                return 0
                ;;

            all)
                SELECTED_ARCHS=("arm64-v8a" "armeabi-v7a" "x86_64")
                return 0
                ;;

            *)
                echo "[ERROR] Unsupported ABI: $1"
                exit 1
                ;;

        esac

    elif [ "$#" -gt 1 ]; then

        echo "[ERROR] Too many arguments"
        exit 1

    fi

    echo "======================================================="
    echo "Android Dependency ABI Selector"
    echo "======================================================="
    echo "  1) arm64-v8a (主流电视/手机)"
    echo "  2) armeabi-v7a (老旧盒子)"
    echo "  3) x86_64 (模拟器)"
    echo "  4) 全部架构"
    echo "  0) 退出"
    echo

    while true; do

        read -r -p "请选择 [0-4]: " CHOICE

        case "$CHOICE" in

            1)
                SELECTED_ARCHS=("arm64-v8a")
                break
                ;;

            2)
                SELECTED_ARCHS=("armeabi-v7a")
                break
                ;;

            3)
                SELECTED_ARCHS=("x86_64")
                break
                ;;

            4)
                SELECTED_ARCHS=("arm64-v8a" "armeabi-v7a" "x86_64")
                break
                ;;

            0)
                exit 0
                ;;

            *)
                echo "[ERROR] 无效选择。"
                ;;

        esac

    done
}

# ============================================================
# Validate source directories
# ============================================================

ALL_DIRS=(
    "$DAVS2_DIR"
    "$UAVS3D_DIR"
    "$OPENSSL_DIR"
    "$DAV1D_DIR"
    "$FREETYPE_DIR"
    "$FRIBIDI_DIR"
    "$HARFBUZZ_DIR"
    "$LIBASS_DIR"
    "$XML2_DIR"
    "$OPUS_DIR"
    "$OGG_DIR"
    "$VORBIS_DIR"
    "$WEBP_DIR"
)

for DIR in "${ALL_DIRS[@]}"; do

    if [ ! -d "$DIR" ]; then
        echo "[ERROR] Source directory not found: $DIR"
        exit 1
    fi

done

# ============================================================
# Validate NDK
# ============================================================

if [ ! -d "$TOOLCHAIN" ] || [ ! -f "$ANDROID_CMAKE_TOOLCHAIN" ]; then

    echo "[ERROR] NDK toolchain infrastructure missing at:"
    echo "        $TOOLCHAIN"

    exit 1

fi

CCHECK_TOOLS=(
    "$LLVM_BIN/clang.exe"
    "$LLVM_BIN/clang++.exe"
    "$LLVM_BIN/llvm-ar.exe"
    "$LLVM_BIN/llvm-ranlib.exe"
    "$LLVM_BIN/llvm-strip.exe"
)

for TOOL in "${CCHECK_TOOLS[@]}"; do

    if [ ! -f "$TOOL" ]; then
        echo "[ERROR] Required NDK tool not found: $TOOL"
        exit 1
    fi

done

NPROC="$(nproc 2>/dev/null || echo 4)"

# ============================================================
# Select ABI
# ============================================================

select_archs "$@"

# ============================================================
# Build
# ============================================================

for ARCH in "${SELECTED_ARCHS[@]}"; do

    echo
    echo "======================================================="
    echo ">>> Building Android ABI: $ARCH"
    echo "======================================================="

    # ========================================================
    # ABI configuration
    # ========================================================

    case "$ARCH" in

        arm64-v8a)

            CLANG_TRIPLE="aarch64-linux-android${API_LEVEL}"
            DAVS2_ARCH="AARCH64"
            UAVS3D_CPU="arm64"
            MESON_CPU_FAMILY="aarch64"
            MESON_CPU="arm64"
            OPUS_HOST="aarch64-linux-android"

            ;;

        armeabi-v7a)

            CLANG_TRIPLE="armv7a-linux-androideabi${API_LEVEL}"
            DAVS2_ARCH="ARM"
            UAVS3D_CPU="armv7"
            MESON_CPU_FAMILY="arm"
            MESON_CPU="armv7-a"
            OPUS_HOST="arm-linux-androideabi"

            ;;

        x86_64)

            CLANG_TRIPLE="x86_64-linux-android${API_LEVEL}"
            DAVS2_ARCH="X86_64"
            UAVS3D_CPU="x86_64"
            MESON_CPU_FAMILY="x86_64"
            MESON_CPU="x86_64"
            OPUS_HOST="x86_64-linux-android"

            ;;

    esac

    # ========================================================
    # NDK compiler
    # ========================================================

    CLANG_EXE="$LLVM_BIN/clang.exe"
    CLANGXX_EXE="$LLVM_BIN/clang++.exe"

    # Keep target-specific launcher paths for Autotools/CMake builds.
    CC="$LLVM_BIN/${CLANG_TRIPLE}-clang"
    CXX="$LLVM_BIN/${CLANG_TRIPLE}-clang++"

    AR="$LLVM_BIN/llvm-ar.exe"
    NM="$LLVM_BIN/llvm-nm.exe"
    RANLIB="$LLVM_BIN/llvm-ranlib.exe"
    STRIP="$LLVM_BIN/llvm-strip.exe"

    # Meson runs as a native Windows program, so executable paths
    # in the cross file must be Windows paths.
    CLANG_EXE_WIN="$(cygpath -m "$CLANG_EXE")"
    CLANGXX_EXE_WIN="$(cygpath -m "$CLANGXX_EXE")"
    AR_WIN="$(cygpath -m "$AR")"
    NM_WIN="$(cygpath -m "$NM")"
    STRIP_WIN="$(cygpath -m "$STRIP")"
    PKG_CONFIG_WIN="$(cygpath -m "$(command -v pkg-config)")"

    # ========================================================
    # Paths
    # ========================================================

    DEPS_PREFIX="$DEPS_ROOT/$ARCH"
    BUILD_PREFIX="$DEPS_BUILD_ROOT/$ARCH"

    echo
    echo ">>> ABI paths"
    echo "    DEPS_PREFIX  = $DEPS_PREFIX"
    echo "    BUILD_PREFIX = $BUILD_PREFIX"
    echo

    # ========================================================
    # Clean ABI directories
    # ========================================================

    rm -rf -- "$DEPS_PREFIX"
    rm -rf -- "$BUILD_PREFIX"

    mkdir -p \
        "$DEPS_PREFIX/include" \
        "$DEPS_PREFIX/lib/pkgconfig" \
        "$BUILD_PREFIX"

    # ========================================================
    # Android system zlib
    # ========================================================

    ANDROID_ZLIB="$("$CLANG_EXE" \
        --target="$CLANG_TRIPLE" \
        --sysroot="$SYSROOT" \
        -print-file-name=libz.so)"

    if [ -z "$ANDROID_ZLIB" ] || [ ! -f "$ANDROID_ZLIB" ]; then

        echo "[ERROR] Android system libz not found."
        echo "        ABI        : $ARCH"
        echo "        target     : $CLANG_TRIPLE"
        echo "        sysroot    : $SYSROOT"
        echo "        result     : $ANDROID_ZLIB"

        exit 1

    fi

    if [ ! -f "$SYSROOT/usr/include/zlib.h" ]; then

        echo "[ERROR] Android zlib.h not found:"
        echo "        $SYSROOT/usr/include/zlib.h"

        exit 1

    fi

    if [ ! -f "$SYSROOT/usr/include/zconf.h" ]; then

        echo "[ERROR] Android zconf.h not found:"
        echo "        $SYSROOT/usr/include/zconf.h"

        exit 1

    fi

    # ========================================================
    # Android dependency isolation
    # ========================================================

    export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig"
    export PKG_CONFIG_PATH="$DEPS_PREFIX/lib/pkgconfig"

    unset PKG_CONFIG_SYSROOT_DIR 2>/dev/null || true

    # ========================================================
    # Synthetic zlib.pc
    #
    # This is NOT a bundled zlib.
    #
    # It only tells pkg-config:
    #
    #   Android provides zlib
    #   link with -lz
    #
    # No MSYS2 zlib path is exposed.
    # ========================================================

    cat > "$DEPS_PREFIX/lib/pkgconfig/zlib.pc" <<EOF
prefix=
exec_prefix=
libdir=
includedir=

Name: zlib
Description: Android NDK system zlib
Version: 1.0
Libs: -lz
Cflags:
EOF

    # ========================================================
    # Meson cross file
    # ========================================================

    CROSS_FILE_PATH="$BUILD_PREFIX/cross_file.txt"
    MESON_CROSS="$CROSS_FILE_PATH"

    DEPS_PREFIX_WIN="$(cygpath -m "$DEPS_PREFIX")"

    cat > "$CROSS_FILE_PATH" <<EOF
[binaries]
c = '$CLANG_EXE_WIN'
cpp = '$CLANGXX_EXE_WIN'
ar = '$AR_WIN'
strip = '$STRIP_WIN'
nm = '$NM_WIN'
pkg-config = '$PKG_CONFIG_WIN'

[host_machine]
system = 'android'
cpu_family = '$MESON_CPU_FAMILY'
cpu = '$MESON_CPU'
endian = 'little'

[built-in options]
c_args = ['--target=$CLANG_TRIPLE']
cpp_args = ['--target=$CLANG_TRIPLE']
c_link_args = ['--target=$CLANG_TRIPLE']
cpp_link_args = ['--target=$CLANG_TRIPLE']

[properties]
needs_exe_wrapper = true
pkg_config_libdir = ['$DEPS_PREFIX_WIN/lib/pkgconfig']
EOF

    # ========================================================
    # DAVS2
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building davs2 [$ARCH]"
    echo "======================================================="

    DAVS2_BUILD="$BUILD_PREFIX/davs2"

    mkdir -p "$DAVS2_BUILD"

    cat > "$DAVS2_BUILD/config.h" <<EOF
#define ARCH_${DAVS2_ARCH} 1
#define SYS_LINUX 1
#define HAVE_POSIXTHREAD 1
#define HAVE_THREAD 1
#define HIGH_BIT_DEPTH 1
#define BIT_DEPTH 10
#define STACK_ALIGNMENT 16
EOF

    DAVS2_SRCS="common/aec.cc common/alf.cc common/bitstream.cc common/block_info.cc common/common.cc common/davs2.cc common/cpu.cc common/cu.cc common/deblock.cc common/decoder.cc common/frame.cc common/header.cc common/intra.cc common/mc.cc common/memory.cc common/pixel.cc common/predict.cc common/quant.cc common/sao.cc common/transform.cc common/primitives.cc common/threadpool.cc common/win32thread.cc"

    for SRC in $DAVS2_SRCS; do

        OBJ_NAME="$(basename "${SRC%.cc}.o")"

        "$CXX" \
            $OPT_FLAGS \
            -fPIC \
            -pthread \
            -D__ANDROID__ \
            -I"$DAVS2_DIR/source" \
            -I"$DAVS2_DIR/source/common" \
            -I"$DAVS2_BUILD" \
            -c "$DAVS2_DIR/source/$SRC" \
            -o "$DAVS2_BUILD/$OBJ_NAME"

    done

    "$AR" rcs \
        "$DEPS_PREFIX/lib/libdavs2.a" \
        "$DAVS2_BUILD"/*.o

    "$RANLIB" \
        "$DEPS_PREFIX/lib/libdavs2.a"

    cp -f \
        "$DAVS2_DIR/source/davs2.h" \
        "$DEPS_PREFIX/include/davs2.h"

    cat > "$DEPS_PREFIX/include/davs2_config.h" <<EOF
#define DAVS2_CHROMA_FORMAT 4
#define BIT_DEPTH 10
#define HIGH_BIT_DEPTH 1
EOF

    cat > "$DEPS_PREFIX/lib/pkgconfig/davs2.pc" <<EOF
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: davs2
Description: AVS2 decoder
Version: 1.6.0
Libs: -L\${libdir} -ldavs2 -lm -ldl
Cflags: -I\${includedir}
EOF

    # ========================================================
    # UAVS3D
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building uavs3d [$ARCH]"
    echo "======================================================="

    UAVS3D_BUILD="$BUILD_PREFIX/uavs3d"

    rm -rf "$UAVS3D_BUILD"
    mkdir -p "$UAVS3D_BUILD"

    pushd "$UAVS3D_BUILD" >/dev/null

    cmake "$UAVS3D_DIR" \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_CMAKE_TOOLCHAIN" \
        -DANDROID_ABI="$ARCH" \
        -DANDROID_PLATFORM="android-$API_LEVEL" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DUAVS3D_TARGET_CPU="$UAVS3D_CPU" \
        -DCOMPILE_10BIT=ON

    make -j"$NPROC" uavs3d

    popd >/dev/null

    cp -f \
        "$UAVS3D_BUILD/source/libuavs3d.a" \
        "$DEPS_PREFIX/lib/libuavs3d.a"

    cp -f \
        "$UAVS3D_DIR/source/decoder/uavs3d.h" \
        "$DEPS_PREFIX/include/uavs3d.h"

    cat > "$DEPS_PREFIX/lib/pkgconfig/uavs3d.pc" <<EOF
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: uavs3d
Description: AVS3 decoder
Version: 1.1.89
Libs: -L\${libdir} -luavs3d -lm
Cflags: -I\${includedir}
EOF

    # ========================================================
    # OpenSSL
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building OpenSSL [$ARCH]"
    echo "======================================================="

    OPENSSL_BUILD="$BUILD_PREFIX/openssl"

    mkdir -p "$OPENSSL_BUILD"

    case "$ARCH" in

        arm64-v8a)
            SSL_TARGET="android-arm64"
            ;;

        armeabi-v7a)
            SSL_TARGET="android-arm"
            ;;

        x86_64)
            SSL_TARGET="android-x86_64"
            ;;

    esac

    export ANDROID_NDK_ROOT="$NDK_PATH"

    OLD_PATH="$PATH"
    export PATH="$LLVM_BIN:$PATH"

    pushd "$OPENSSL_DIR" >/dev/null

    make clean || true

    ./Configure \
        "$SSL_TARGET" \
        -D__ANDROID_API__="$API_LEVEL" \
        no-shared \
        no-tests \
        --prefix="$DEPS_PREFIX" \
        --openssldir="$DEPS_PREFIX/ssl"

    make -j"$NPROC"

    make install_sw

    popd >/dev/null

    export PATH="$OLD_PATH"

    unset ANDROID_NDK_ROOT

    # ========================================================
    # dav1d
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building dav1d [$ARCH]"
    echo "======================================================="

    DAV1D_BUILD="$BUILD_PREFIX/dav1d"

    rm -rf "$DAV1D_BUILD"
    mkdir -p "$DAV1D_BUILD"

    "$MESON_BIN" setup \
        "$DAV1D_BUILD" \
        "$DAV1D_DIR" \
        --cross-file "$MESON_CROSS" \
        --libdir lib \
        --default-library static \
        --buildtype release \
        -Denable_tests=false \
        -Denable_asm=true

    "$NINJA_BIN" \
        -C "$DAV1D_BUILD" \
        -j"$NPROC"

    mkdir -p \
        "$DEPS_PREFIX/include/dav1d" \
        "$DEPS_PREFIX/lib" \
        "$DEPS_PREFIX/lib/pkgconfig"

    if [ ! -f "$DAV1D_BUILD/src/libdav1d.a" ]; then
        echo "[ERROR] dav1d static library not found:"
        echo "        $DAV1D_BUILD/src/libdav1d.a"
        exit 1
    fi

    cp -f \
        "$DAV1D_BUILD/src/libdav1d.a" \
        "$DEPS_PREFIX/lib/libdav1d.a"

    DAV1D_HEADERS=(
        dav1d.h
        common.h
        data.h
        headers.h
        picture.h
        version.h
    )

    for HEADER in "${DAV1D_HEADERS[@]}"; do

        if [ -f "$DAV1D_DIR/include/dav1d/$HEADER" ]; then

            cp -f \
                "$DAV1D_DIR/include/dav1d/$HEADER" \
                "$DEPS_PREFIX/include/dav1d/$HEADER"

        fi

    done

    cat > "$DEPS_PREFIX/lib/pkgconfig/dav1d.pc" <<EOF
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: dav1d
Description: AV1 decoder
Version: 1.5.4
Libs: -L\${libdir} -ldav1d
Cflags: -I\${includedir}
EOF

    # ========================================================
    # FreeType
    #
    # FreeType uses Android NDK system zlib.
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building freetype [$ARCH]"
    echo "======================================================="

    FREETYPE_BUILD="$BUILD_PREFIX/freetype"

    rm -rf "$FREETYPE_BUILD"
    mkdir -p "$FREETYPE_BUILD"

    pushd "$FREETYPE_BUILD" >/dev/null

    cmake "$FREETYPE_DIR" \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_CMAKE_TOOLCHAIN" \
        -DANDROID_ABI="$ARCH" \
        -DANDROID_PLATFORM="android-$API_LEVEL" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_C_FLAGS="$OPT_FLAGS -fPIC" \
        -DCMAKE_CXX_FLAGS="$OPT_FLAGS -fPIC" \
        -DFT_WITH_ZLIB=ON \
        -DFT_WITH_BZIP2=OFF \
        -DFT_WITH_PNG=OFF \
        -DFT_WITH_HARFBUZZ=OFF \
        -DZLIB_INCLUDE_DIR="$SYSROOT/usr/include" \
        -DZLIB_LIBRARY="$ANDROID_ZLIB"

    make -j"$NPROC"

    make install

    popd >/dev/null

    # ========================================================
    # Verify FreeType pkg-config dependency
    # ========================================================

    if [ -f "$DEPS_PREFIX/lib/pkgconfig/freetype2.pc" ]; then

        echo
        echo ">>> FreeType pkg-config:"
        echo "    $DEPS_PREFIX/lib/pkgconfig/freetype2.pc"

        echo
        echo ">>> FreeType static libraries:"
        PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig" \
            PKG_CONFIG_PATH="$DEPS_PREFIX/lib/pkgconfig" \
            pkg-config --libs --static freetype2

    else

        echo "[WARNING] freetype2.pc not found:"
        echo "          $DEPS_PREFIX/lib/pkgconfig/freetype2.pc"

    fi

    # ========================================================
    # FriBidi
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building fribidi [$ARCH]"
    echo "======================================================="

    FRIBIDI_BUILD="$BUILD_PREFIX/fribidi"

    rm -rf "$FRIBIDI_BUILD"
    mkdir -p "$FRIBIDI_BUILD"

    "$MESON_BIN" setup \
        "$FRIBIDI_BUILD" \
        "$FRIBIDI_DIR" \
        --cross-file "$MESON_CROSS" \
        --libdir lib \
        --default-library static \
        --buildtype release \
        -Dtests=false \
        -Ddocs=false

    "$NINJA_BIN" \
        -C "$FRIBIDI_BUILD" \
        -j"$NPROC"

    mkdir -p \
        "$DEPS_PREFIX/include/fribidi" \
        "$DEPS_PREFIX/lib" \
        "$DEPS_PREFIX/lib/pkgconfig"

    FRIBIDI_LIB=""

    if [ -f "$FRIBIDI_BUILD/lib/libfribidi.a" ]; then
        FRIBIDI_LIB="$FRIBIDI_BUILD/lib/libfribidi.a"
    elif [ -f "$FRIBIDI_BUILD/src/libfribidi.a" ]; then
        FRIBIDI_LIB="$FRIBIDI_BUILD/src/lib/libfribidi.a"
    else
        FRIBIDI_LIB="$(find "$FRIBIDI_BUILD" \
            -name 'libfribidi.a' \
            -print -quit)"
    fi

    if [ -z "$FRIBIDI_LIB" ] || [ ! -f "$FRIBIDI_LIB" ]; then
        echo "[ERROR] fribidi static library not found."
        echo "        Build directory: $FRIBIDI_BUILD"
        exit 1
    fi

    cp -f \
        "$FRIBIDI_LIB" \
        "$DEPS_PREFIX/lib/libfribidi.a"

    # --------------------------------------------------------
    # Copy FriBidi source/public headers
    # --------------------------------------------------------

    if [ -d "$FRIBIDI_DIR/lib" ]; then

        find "$FRIBIDI_DIR/lib" \
            -maxdepth 1 \
            -type f \
            -name '*.h' \
            -exec cp -f {} "$DEPS_PREFIX/include/fribidi/" \;

    fi

    # --------------------------------------------------------
    # Copy FriBidi generated headers
    #
    # Generated headers may be located in different directories
    # inside the Meson build tree.
    #
    # Known examples:
    #
    #   lib/fribidi-config.h
    #   gen.tab/fribidi-unicode-version.h
    #
    # Search recursively instead of assuming a fixed directory.
    # --------------------------------------------------------

    while IFS= read -r FRIBIDI_GENERATED_HEADER; do

        if [ -f "$FRIBIDI_GENERATED_HEADER" ]; then

            cp -f \
                "$FRIBIDI_GENERATED_HEADER" \
                "$DEPS_PREFIX/include/fribidi/"

        fi

    done < <(
        find "$FRIBIDI_BUILD" \
            -type f \
            -name '*.h' \
            \( \
                -name 'fribidi-config.h' \
                -o \
                -name 'fribidi-unicode-version.h' \
            \)
    )

    # --------------------------------------------------------
    # Verify required FriBidi generated headers
    # --------------------------------------------------------

    if [ ! -f "$DEPS_PREFIX/include/fribidi/fribidi-config.h" ]; then

        echo "[ERROR] Missing installed FriBidi header:"
        echo "        $DEPS_PREFIX/include/fribidi/fribidi-config.h"

        exit 1

    fi

    if [ ! -f "$DEPS_PREFIX/include/fribidi/fribidi-unicode-version.h" ]; then

        echo "[ERROR] Missing installed FriBidi header:"
        echo "        $DEPS_PREFIX/include/fribidi/fribidi-unicode-version.h"

        exit 1

    fi

    # ========================================================
    # FriBidi pkg-config
    #
    # Public header:
    #
    #   include/fribidi/fribidi.h
    #
    # Therefore:
    #
    #   Cflags: -I${includedir}/fribidi
    # ========================================================

    cat > "$DEPS_PREFIX/lib/pkgconfig/fribidi.pc" <<EOF
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: FriBidi
Description: Unicode Bidirectional Algorithm library
Version: 1.0.16
Libs: -L\${libdir} -lfribidi
Cflags: -I\${includedir}/fribidi
EOF

    echo
    echo ">>> Verify FriBidi pkg-config"
    echo "    cflags:"
    pkg-config --cflags fribidi

    echo "    libs:"
    pkg-config --libs --static fribidi

    # ========================================================
    # HarfBuzz
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building harfbuzz [$ARCH]"
    echo "======================================================="

    HARFBUZZ_BUILD="$BUILD_PREFIX/harfbuzz"

    rm -rf "$HARFBUZZ_BUILD"
    mkdir -p "$HARFBUZZ_BUILD"

    # Keep Android-only dependency isolation.
    export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig"
    export PKG_CONFIG_PATH="$DEPS_PREFIX/lib/pkgconfig"

    echo
    echo ">>> Checking FreeType dependency"
    pkg-config --modversion freetype2
    pkg-config --cflags freetype2
    pkg-config --libs --static freetype2

    echo
    echo ">>> Checking FriBidi dependency"
    pkg-config --modversion fribidi
    pkg-config --cflags fribidi
    pkg-config --libs --static fribidi

    echo
    echo ">>> Checking Android system zlib dependency"
    pkg-config --modversion zlib
    pkg-config --libs zlib

    "$MESON_BIN" setup \
        "$HARFBUZZ_BUILD" \
        "$HARFBUZZ_DIR" \
        --cross-file "$MESON_CROSS" \
        --libdir lib \
        --default-library static \
        --buildtype release \
        -Dtests=disabled \
        -Dbenchmark=disabled \
        -Ddocs=disabled \
        -Dglib=disabled \
        -Dgobject=disabled \
        -Dutilities=disabled \
        -Dcairo=disabled \
        -Dgraphite2=disabled \
        -Dicu=disabled \
        -Dchafa=disabled \
        -Dfreetype=enabled

    "$NINJA_BIN" \
        -C "$HARFBUZZ_BUILD" \
        -j"$NPROC"

    # ========================================================
    # Locate HarfBuzz static library
    # ========================================================

    mkdir -p \
        "$DEPS_PREFIX/include/harfbuzz" \
        "$DEPS_PREFIX/lib" \
        "$DEPS_PREFIX/lib/pkgconfig"

    HARFBUZZ_LIB=""

    if [ -f "$HARFBUZZ_BUILD/src/libharfbuzz.a" ]; then
        HARFBUZZ_LIB="$HARFBUZZ_BUILD/src/libharfbuzz.a"
    elif [ -f "$HARFBUZZ_BUILD/lib/libharfbuzz.a" ]; then
        HARFBUZZ_LIB="$HARFBUZZ_BUILD/lib/libharfbuzz.a"
    else
        HARFBUZZ_LIB="$(find "$HARFBUZZ_BUILD" \
            -name 'libharfbuzz.a' \
            -print -quit)"
    fi

    if [ -z "$HARFBUZZ_LIB" ] || [ ! -f "$HARFBUZZ_LIB" ]; then
        echo "[ERROR] harfbuzz static library not found."
        echo "        Build directory: $HARFBUZZ_BUILD"
        exit 1
    fi

    cp -f \
        "$HARFBUZZ_LIB" \
        "$DEPS_PREFIX/lib/libharfbuzz.a"

    # ========================================================
    # Copy public HarfBuzz headers
    # ========================================================

    if [ -d "$HARFBUZZ_DIR/src" ]; then

        find "$HARFBUZZ_DIR/src" \
            -maxdepth 1 \
            -type f \
            \( -name 'hb*.h' \) \
            -exec cp -f {} \
                "$DEPS_PREFIX/include/harfbuzz/" \;

    fi

    # ========================================================
    # HarfBuzz pkg-config
    # ========================================================

    cat > "$DEPS_PREFIX/lib/pkgconfig/harfbuzz.pc" <<EOF
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: HarfBuzz
Description: OpenType text shaping engine
Version: 14.4.0
Requires.private: freetype2
Libs: -L\${libdir} -lharfbuzz
Cflags: -I\${includedir}/harfbuzz
EOF

    # ========================================================
    # libass
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building libass [$ARCH]"
    echo "======================================================="

    LIBASS_BUILD="$BUILD_PREFIX/libass"

    rm -rf "$LIBASS_BUILD"
    mkdir -p "$LIBASS_BUILD"

    if [ ! -f "$LIBASS_DIR/configure" ]; then

        pushd "$LIBASS_DIR" >/dev/null

        ./autogen.sh

        popd >/dev/null

    fi

    export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig"
    export PKG_CONFIG_PATH="$DEPS_PREFIX/lib/pkgconfig"

    # ========================================================
    # Explicit include paths
    # ========================================================

    export CPPFLAGS="-I$DEPS_PREFIX/include \
-I$DEPS_PREFIX/include/fribidi \
-I$DEPS_PREFIX/include/harfbuzz \
-I$DEPS_PREFIX/include/freetype2"

    export CFLAGS="$OPT_FLAGS -fPIC \
-I$DEPS_PREFIX/include \
-I$DEPS_PREFIX/include/fribidi \
-I$DEPS_PREFIX/include/harfbuzz \
-I$DEPS_PREFIX/include/freetype2"

    export LDFLAGS="-L$DEPS_PREFIX/lib"

    echo
    echo ">>> libass dependency cflags"

    echo "[FriBidi]"
    pkg-config --cflags fribidi

    echo "[FreeType]"
    pkg-config --cflags freetype2

    echo "[HarfBuzz]"
    pkg-config --cflags harfbuzz

    pushd "$LIBASS_BUILD" >/dev/null

    "$LIBASS_DIR/configure" \
        --host="$CLANG_TRIPLE" \
        --prefix="$DEPS_PREFIX" \
        --disable-shared \
        --enable-static \
        --disable-fontconfig \
        --disable-require-system-font-provider \
        CC="$CC" \
        CXX="$CXX" \
        AR="$AR" \
        RANLIB="$RANLIB" \
        STRIP="$STRIP" \
        CFLAGS="$CFLAGS" \
        CPPFLAGS="$CPPFLAGS" \
        LDFLAGS="$LDFLAGS"

    make -j"$NPROC"

    make install

    popd >/dev/null

    # ========================================================
    # libxml2
    #
    # Use Android system zlib.
    #
    # No MSYS2 zlib is allowed.
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building libxml2 [$ARCH]"
    echo "======================================================="

    XML2_BUILD="$BUILD_PREFIX/libxml2"

    rm -rf "$XML2_BUILD"
    mkdir -p "$XML2_BUILD"

    pushd "$XML2_BUILD" >/dev/null

    cmake "$XML2_DIR" \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_CMAKE_TOOLCHAIN" \
        -DANDROID_ABI="$ARCH" \
        -DANDROID_PLATFORM="android-$API_LEVEL" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DLIBXML2_WITH_PYTHON=OFF \
        -DLIBXML2_WITH_LZMA=OFF \
        -DLIBXML2_WITH_ZLIB=ON \
        -DLIBXML2_WITH_ICONV=OFF \
        -DZLIB_INCLUDE_DIR="$SYSROOT/usr/include" \
        -DZLIB_LIBRARY="$ANDROID_ZLIB"

    make -j"$NPROC"

    make install

    popd >/dev/null

    # ========================================================
    # Opus
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building opus [$ARCH]"
    echo "======================================================="

    OPUS_BUILD="$BUILD_PREFIX/opus"

    rm -rf "$OPUS_BUILD"
    mkdir -p "$OPUS_BUILD"

	if [ "$ARCH" = "armeabi-v7a" ]; then
		echo ">>> Normalizing Opus ARM assembly sources to LF"

		find "$OPUS_DIR/celt/arm" \
			-type f \
			\( -name '*.S' -o -name '*.s' \) \
			-exec sh -c '
				for f do
					tmp="${f}.lf"
					tr -d "\r" < "$f" > "$tmp" &&
					mv "$tmp" "$f"
				done
			' sh {} +

		echo ">>> Opus ARM assembly line endings normalized"
	fi

    if [ ! -f "$OPUS_DIR/configure" ]; then

        pushd "$OPUS_DIR" >/dev/null

        ./autogen.sh

        popd >/dev/null

    fi

    export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig"
    export PKG_CONFIG_PATH="$DEPS_PREFIX/lib/pkgconfig"

    pushd "$OPUS_BUILD" >/dev/null

    "$OPUS_DIR/configure" \
        --host="$OPUS_HOST" \
        --prefix="$DEPS_PREFIX" \
        --disable-shared \
        --enable-static \
        --disable-extra-programs \
        --disable-doc \
        CC="$CC" \
        AR="$AR" \
        RANLIB="$RANLIB" \
        STRIP="$STRIP" \
        CFLAGS="$OPT_FLAGS -fPIC"

    make -j"$NPROC"

    make install

    popd >/dev/null

    # ========================================================
    # libogg
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building libogg [$ARCH]"
    echo "======================================================="

    OGG_BUILD="$BUILD_PREFIX/libogg"

    rm -rf "$OGG_BUILD"
    mkdir -p "$OGG_BUILD"

    pushd "$OGG_BUILD" >/dev/null

    cmake "$OGG_DIR" \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_CMAKE_TOOLCHAIN" \
        -DANDROID_ABI="$ARCH" \
        -DANDROID_PLATFORM="android-$API_LEVEL" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
        -DBUILD_SHARED_LIBS=OFF

    make -j"$NPROC"

    make install

    popd >/dev/null

    # ========================================================
    # libvorbis
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building libvorbis [$ARCH]"
    echo "======================================================="

    VORBIS_BUILD="$BUILD_PREFIX/libvorbis"

    rm -rf "$VORBIS_BUILD"
    mkdir -p "$VORBIS_BUILD"

    # --------------------------------------------------------
    # libvorbis depends on the Android Ogg built above.
    #
    # Explicitly provide both variables expected by
    # cmake/FindOgg.cmake:
    #
    #   OGG_LIBRARY
    #   OGG_INCLUDE_DIR
    #
    # Keep OGG_ROOT as well for compatibility with projects
    # that inspect it.
    # --------------------------------------------------------

    if [ ! -f "$DEPS_PREFIX/lib/libogg.a" ]; then

        echo "[ERROR] libogg.a not found:"
        echo "        $DEPS_PREFIX/lib/libogg.a"

        exit 1

    fi

    if [ ! -f "$DEPS_PREFIX/include/ogg/ogg.h" ]; then

        echo "[ERROR] ogg.h not found:"
        echo "        $DEPS_PREFIX/include/ogg/ogg.h"

        exit 1

    fi

    pushd "$VORBIS_BUILD" >/dev/null

    cmake "$VORBIS_DIR" \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_CMAKE_TOOLCHAIN" \
        -DANDROID_ABI="$ARCH" \
        -DANDROID_PLATFORM="android-$API_LEVEL" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DOGG_ROOT="$DEPS_PREFIX" \
        -DOGG_LIBRARY="$DEPS_PREFIX/lib/libogg.a" \
        -DOGG_INCLUDE_DIR="$DEPS_PREFIX/include"

    make -j"$NPROC"
	make install

	# --------------------------------------------------------
	# Fix pkg-config metadata for static linking.
	#
	# libvorbis depends on libogg. The generated vorbis.pc
	# may not expose -logg correctly, which causes FFmpeg's
	# configure link test to fail with undefined symbols such
	# as oggpack_write / oggpack_read.
	#
	# Use an MSYS-style absolute prefix so pkg-config can
	# resolve the dependency cleanly in the current build
	# environment.
	# --------------------------------------------------------

	VORBIS_PC_DIR="$DEPS_PREFIX/lib/pkgconfig"

	mkdir -p "$VORBIS_PC_DIR"

	cat > "$VORBIS_PC_DIR/vorbis.pc" <<EOF
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: vorbis
Description: Ogg Vorbis audio codec library
Version: 1.3.7
Libs: -L\${libdir} -lvorbis -logg
Libs.private: -lm
Cflags: -I\${includedir}
EOF

	cat > "$VORBIS_PC_DIR/vorbisenc.pc" <<EOF
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: vorbisenc
Description: Ogg Vorbis encoder library
Version: 1.3.7
Libs: -L\${libdir} -lvorbisenc -lvorbis -logg
Libs.private: -lm
Cflags: -I\${includedir}
EOF

	cat > "$VORBIS_PC_DIR/vorbisfile.pc" <<EOF
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: vorbisfile
Description: Ogg Vorbis file access library
Version: 1.3.7
Libs: -L\${libdir} -lvorbisfile -lvorbis -logg
Libs.private: -lm
Cflags: -I\${includedir}
EOF

	echo ">>> Fixed libvorbis pkg-config files:"
	echo "    $VORBIS_PC_DIR/vorbis.pc"
	echo "    $VORBIS_PC_DIR/vorbisenc.pc"
	echo "    $VORBIS_PC_DIR/vorbisfile.pc"

	# Verify that static dependency libogg is exposed.
	echo ">>> Vorbis pkg-config:"
	echo "    $(PKG_CONFIG_PATH="$VORBIS_PC_DIR" pkg-config --cflags --libs vorbis)"

	echo ">>> VorbisEnc pkg-config:"
	echo "    $(PKG_CONFIG_PATH="$VORBIS_PC_DIR" pkg-config --cflags --libs vorbisenc)"

	echo ">>> VorbisFile pkg-config:"
	echo "    $(PKG_CONFIG_PATH="$VORBIS_PC_DIR" pkg-config --cflags --libs vorbisfile)"

	popd >/dev/null

    # ========================================================
    # libwebp
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building libwebp [$ARCH]"
    echo "======================================================="

    WEBP_BUILD="$BUILD_PREFIX/libwebp"

    rm -rf "$WEBP_BUILD"
    mkdir -p "$WEBP_BUILD"

    pushd "$WEBP_BUILD" >/dev/null

    cmake "$WEBP_DIR" \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_CMAKE_TOOLCHAIN" \
        -DANDROID_ABI="$ARCH" \
        -DANDROID_PLATFORM="android-$API_LEVEL" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DWEBP_BUILD_ANIM_UTILS=OFF \
        -DWEBP_BUILD_CWEBP=OFF \
        -DWEBP_BUILD_DWEBP=OFF \
        -DWEBP_BUILD_GIF2WEBP=OFF \
        -DWEBP_BUILD_IMG2WEBP=OFF \
        -DWEBP_BUILD_VWEBP=OFF \
        -DWEBP_BUILD_WEBPINFO=OFF \
        -DWEBP_BUILD_WEBPMUX=OFF \
        -DWEBP_BUILD_EXTRAS=OFF

    make -j"$NPROC"

    make install

	# --------------------------------------------------------
	# Fix libwebp pkg-config metadata for static linking.
	#
	# libwebp.a references SharpYuvInit from libsharpyuv.a.
	# FFmpeg's configure uses normal pkg-config --libs,
	# so libsharpyuv must be exposed through Libs rather than
	# only Requires.private / Libs.private.
	#
	# Also remove the generated -pthread dependency because
	# this is an Android NDK build.
	# --------------------------------------------------------

	WEBP_PC_DIR="$DEPS_PREFIX/lib/pkgconfig"

	mkdir -p "$WEBP_PC_DIR"

	cat > "$WEBP_PC_DIR/libwebp.pc" <<EOF
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libwebp
Description: Library for the WebP graphics format
Version: 1.6.0
Cflags: -I\${includedir}
Libs: -L\${libdir} -lwebp -lsharpyuv
Libs.private: -lm
EOF

	echo ">>> Fixed libwebp pkg-config:"
	echo "    $WEBP_PC_DIR/libwebp.pc"

	echo ">>> libwebp version:"
	echo "    $(pkg-config --modversion libwebp)"

	echo ">>> libwebp flags:"
	echo "    $(pkg-config --cflags --libs libwebp)"

    popd >/dev/null

    # ========================================================
    # Verify output
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Verifying [$ARCH]"
    echo "======================================================="

    REQUIRED_FILES=(
        "$DEPS_PREFIX/lib/libdavs2.a"
        "$DEPS_PREFIX/lib/libuavs3d.a"
        "$DEPS_PREFIX/lib/libssl.a"
        "$DEPS_PREFIX/lib/libcrypto.a"
        "$DEPS_PREFIX/lib/libdav1d.a"
        "$DEPS_PREFIX/lib/libfreetype.a"
        "$DEPS_PREFIX/lib/libfribidi.a"
        "$DEPS_PREFIX/lib/libharfbuzz.a"
        "$DEPS_PREFIX/lib/libass.a"
        "$DEPS_PREFIX/lib/libxml2.a"
        "$DEPS_PREFIX/lib/libopus.a"
        "$DEPS_PREFIX/lib/libogg.a"
        "$DEPS_PREFIX/lib/libvorbis.a"
        "$DEPS_PREFIX/lib/libwebp.a"
    )

    for FILE in "${REQUIRED_FILES[@]}"; do

        if [ ! -f "$FILE" ]; then

            echo
            echo "[ERROR] Missing required file:"
            echo "        $FILE"

            exit 1

        fi

        echo "[OK] $FILE"

    done

    # ========================================================
    # Final dependency verification
    # ========================================================

    echo
    echo ">>> Final pkg-config verification"

    export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig"
    export PKG_CONFIG_PATH="$DEPS_PREFIX/lib/pkgconfig"

    echo
    echo "[zlib]"
    pkg-config --modversion zlib
    pkg-config --libs zlib

    echo
    echo "[freetype2]"
    pkg-config --modversion freetype2
    pkg-config --cflags freetype2
    pkg-config --libs --static freetype2

    echo
    echo "[fribidi]"
    pkg-config --modversion fribidi
    pkg-config --cflags fribidi
    pkg-config --libs --static fribidi

    echo
    echo "[harfbuzz]"
    pkg-config --modversion harfbuzz
    pkg-config --cflags harfbuzz
    pkg-config --libs --static harfbuzz

    echo
    echo "[libass]"
    pkg-config --modversion libass 2>/dev/null || true
    pkg-config --cflags libass 2>/dev/null || true
    pkg-config --libs --static libass 2>/dev/null || true

    echo
    echo ">>> Android system zlib:"
    echo "    $ANDROID_ZLIB"

    echo
    echo ">>> All required files for $ARCH exist."
    echo

done

# ============================================================
# Completed
# ============================================================

echo
echo "======================================================="
echo ">>> Android third-party build completed successfully!"
echo "======================================================="
