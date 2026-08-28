#!/bin/bash

# ============================================================
# FFmpeg AVS Project
# Android Third-Party Dependencies Build
#
# Build:
#   libdavs2
#   libuavs3d
#
# ABI:
#   arm64-v8a
#   armeabi-v7a
#   x86_64
#
# Environment:
#   MSYS2 MINGW64
#
# Configuration:
#   build_scripts/env.sh
#
# Usage:
#
#   ./build_deps_android.sh
#       Interactive ABI selector
#
#   ./build_deps_android.sh arm64-v8a
#       Build arm64-v8a only
#
#   ./build_deps_android.sh armeabi-v7a
#       Build armeabi-v7a only
#
#   ./build_deps_android.sh x86_64
#       Build x86_64 only
#
#   ./build_deps_android.sh all
#       Build all ABIs
# ============================================================

set -euo pipefail

# ============================================================
# 1. Project root
# ============================================================

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$ROOT_DIR"

# ============================================================
# 2. Load common environment
# ============================================================

ENV_FILE="$ROOT_DIR/build_scripts/env.sh"

if [ ! -f "$ENV_FILE" ]; then
    echo "[ERROR] env.sh not found:"
    echo "        $ENV_FILE"
    exit 1
fi

source "$ENV_FILE"

# ============================================================
# 3. Validate environment
# ============================================================

: "${PROJECT_ROOT:?PROJECT_ROOT is not set}"
: "${NDK_PATH:?NDK_PATH is not set}"
: "${API_LEVEL:?API_LEVEL is not set}"
: "${ANDROID_DEPS_ROOT:?ANDROID_DEPS_ROOT is not set}"

# ============================================================
# 4. Source directories
# ============================================================

DAVS2_DIR="$PROJECT_ROOT/davs2"
UAVS3D_DIR="$PROJECT_ROOT/uavs3d"

# ============================================================
# 5. Android NDK toolchain
# ============================================================

HOST_TAG="windows-x86_64"

TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG"
LLVM_BIN="$TOOLCHAIN/bin"
SYSROOT="$TOOLCHAIN/sysroot"

ANDROID_CMAKE_TOOLCHAIN="$NDK_PATH/build/cmake/android.toolchain.cmake"

# ============================================================
# 6. Build / output directories
# ============================================================

DEPS_BUILD_ROOT="$PROJECT_ROOT/android_build_deps"
DEPS_ROOT="$ANDROID_DEPS_ROOT"

# ============================================================
# 7. MSYS2 PATH isolation
#
# Do NOT put NDK/bin before /mingw64/bin.
# ============================================================

export PATH="/mingw64/bin:/usr/local/bin:/usr/bin:/bin"

# ============================================================
# 8. ABI selector
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
                SELECTED_ARCHS=(
                    "arm64-v8a"
                    "armeabi-v7a"
                    "x86_64"
                )
                return 0
                ;;

            *)
                echo "[ERROR] Unsupported ABI: $1"
                echo
                echo "Supported:"
                echo "  arm64-v8a"
                echo "  armeabi-v7a"
                echo "  x86_64"
                echo "  all"
                exit 1
                ;;

        esac

    elif [ "$#" -gt 1 ]; then

        echo "[ERROR] Too many arguments"
        echo
        echo "Usage:"
        echo "  $0"
        echo "  $0 arm64-v8a"
        echo "  $0 armeabi-v7a"
        echo "  $0 x86_64"
        echo "  $0 all"
        exit 1

    fi

    echo
    echo "======================================================="
    echo "Android Dependency ABI Selector"
    echo "======================================================="
    echo
    echo "  1) arm64-v8a"
    echo "  2) armeabi-v7a"
    echo "  3) x86_64"
    echo "  4) All ABIs"
    echo "  0) Exit"
    echo

    while true; do

        read -r -p "Select [0-4]: " CHOICE

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
                SELECTED_ARCHS=(
                    "arm64-v8a"
                    "armeabi-v7a"
                    "x86_64"
                )
                break
                ;;

            0)
                echo
                echo "Exited."
                exit 0
                ;;

            *)
                echo "[ERROR] Invalid selection. Please enter 0-4."
                ;;

        esac

    done
}

# ============================================================
# 9. Check required directories
# ============================================================

echo
echo "======================================================="
echo "Android Dependency Build"
echo "======================================================="
echo
echo "PROJECT_ROOT      : $PROJECT_ROOT"
echo "TOOLCHAIN         : $TOOLCHAIN"
echo "ANDROID_DEPS_ROOT : $DEPS_ROOT"
echo "DEPS_BUILD_ROOT   : $DEPS_BUILD_ROOT"
echo "NDK_PATH          : $NDK_PATH"
echo "API_LEVEL         : $API_LEVEL"
echo

if [ ! -d "$DAVS2_DIR" ]; then
    echo "[ERROR] DAVS2 source directory not found:"
    echo "        $DAVS2_DIR"
    exit 1
fi

if [ ! -d "$UAVS3D_DIR" ]; then
    echo "[ERROR] UAVS3D source directory not found:"
    echo "        $UAVS3D_DIR"
    exit 1
fi

if [ ! -d "$TOOLCHAIN" ]; then
    echo "[ERROR] Android NDK toolchain not found:"
    echo "        $TOOLCHAIN"
    exit 1
fi

if [ ! -f "$ANDROID_CMAKE_TOOLCHAIN" ]; then
    echo "[ERROR] Android CMake toolchain not found:"
    echo "        $ANDROID_CMAKE_TOOLCHAIN"
    exit 1
fi

# ============================================================
# 10. Check NDK tools
# ============================================================

CCHECK_TOOLS=(
    "$LLVM_BIN/llvm-ar.exe"
    "$LLVM_BIN/llvm-ranlib.exe"
    "$LLVM_BIN/llvm-strip.exe"
)

for TOOL in "${CCHECK_TOOLS[@]}"; do
    if [ ! -f "$TOOL" ]; then
        echo "[ERROR] Required NDK tool not found:"
        echo "        $TOOL"
        exit 1
    fi
done

# ============================================================
# 11. Parallel jobs
# ============================================================

NPROC="$(nproc 2>/dev/null || echo 4)"

echo ">>> Parallel jobs: $NPROC"
echo

# ============================================================
# 12. Select ABI
# ============================================================

select_archs "$@"

echo
echo ">>> Selected ABI(s):"

for ARCH in "${SELECTED_ARCHS[@]}"; do
    echo "    $ARCH"
done

echo

# ============================================================
# 13. Build selected ABI(s)
# ============================================================

for ARCH in "${SELECTED_ARCHS[@]}"; do

    echo
    echo "======================================================="
    echo ">>> Building Android ABI: $ARCH"
    echo "======================================================="
    echo

    # ========================================================
    # ABI-specific configuration
    # ========================================================

    case "$ARCH" in

        arm64-v8a)

            CLANG_TRIPLE="aarch64-linux-android${API_LEVEL}"
            DAVS2_ARCH="AARCH64"
            UAVS3D_CPU="arm64"
            UAVS3D_PROCESSOR="aarch64"

            ;;

        armeabi-v7a)

            CLANG_TRIPLE="armv7a-linux-androideabi${API_LEVEL}"
            DAVS2_ARCH="ARM"
            UAVS3D_CPU="armv7"
            UAVS3D_PROCESSOR="arm"

            ;;

        x86_64)

            CLANG_TRIPLE="x86_64-linux-android${API_LEVEL}"
            DAVS2_ARCH="X86_64"
            UAVS3D_CPU="x86_64"
            UAVS3D_PROCESSOR="x86_64"

            ;;

        *)

            echo "[ERROR] Unsupported Android ABI: $ARCH"
            exit 1

            ;;

    esac

    # ========================================================
    # Compiler paths
    # ========================================================

    CC="$LLVM_BIN/${CLANG_TRIPLE}-clang"
    CXX="$LLVM_BIN/${CLANG_TRIPLE}-clang++"

    AR="$LLVM_BIN/llvm-ar"
    RANLIB="$LLVM_BIN/llvm-ranlib"
    STRIP="$LLVM_BIN/llvm-strip"

    # ========================================================
    # Per-ABI directories
    # ========================================================

    DEPS_PREFIX="$DEPS_ROOT/$ARCH"
    BUILD_PREFIX="$DEPS_BUILD_ROOT/$ARCH"

    DAVS2_BUILD="$BUILD_PREFIX/davs2"
    UAVS3D_BUILD="$BUILD_PREFIX/uavs3d"

    # ========================================================
    # Only clean current ABI
    # ========================================================

    rm -rf -- "$DEPS_PREFIX"
    rm -rf -- "$BUILD_PREFIX"

    mkdir -p -- "$DEPS_PREFIX/include"
    mkdir -p -- "$DEPS_PREFIX/lib"
    mkdir -p -- "$DEPS_PREFIX/lib/pkgconfig"

    mkdir -p -- "$DAVS2_BUILD"
    mkdir -p -- "$UAVS3D_BUILD"

    # ========================================================
    # Compiler validation
    # ========================================================

    if [ ! -f "$CC" ]; then
        echo "[ERROR] C compiler not found:"
        echo "        $CC"
        exit 1
    fi

    if [ ! -f "$CXX" ]; then
        echo "[ERROR] C++ compiler not found:"
        echo "        $CXX"
        exit 1
    fi

    echo ">>> CC     : $CC"
    echo ">>> CXX    : $CXX"
    echo ">>> AR     : $AR"
    echo ">>> RANLIB : $RANLIB"
    echo

    # ========================================================
    # 14. Build libdavs2
    # ========================================================

    echo
    echo "-------------------------------------------------------"
    echo ">>> Building libdavs2: $ARCH"
    echo "-------------------------------------------------------"
    echo

    cat > "$DAVS2_BUILD/config.h" <<EOF
#define ARCH_${DAVS2_ARCH} 1
#define SYS_LINUX 1
#define HAVE_POSIXTHREAD 1
#define HAVE_THREAD 1
#define HIGH_BIT_DEPTH 1
#define BIT_DEPTH 10
#define STACK_ALIGNMENT 16
EOF

    DAVS2_SRCS="
common/aec.cc
common/alf.cc
common/bitstream.cc
common/block_info.cc
common/common.cc
common/davs2.cc
common/cpu.cc
common/cu.cc
common/deblock.cc
common/decoder.cc
common/frame.cc
common/header.cc
common/intra.cc
common/mc.cc
common/memory.cc
common/pixel.cc
common/predict.cc
common/quant.cc
common/sao.cc
common/transform.cc
common/primitives.cc
common/threadpool.cc
common/win32thread.cc
"

    for SRC in $DAVS2_SRCS; do

        OBJ_NAME="$(basename "${SRC%.cc}.o")"

        echo ">>> DAVS2: $SRC"

        "$CXX" \
            -O3 \
            -fPIC \
            -pthread \
            -D__ANDROID__ \
            -I"$DAVS2_DIR/source" \
            -I"$DAVS2_DIR/source/common" \
            -I"$DAVS2_BUILD" \
            -c "$DAVS2_DIR/source/$SRC" \
            -o "$DAVS2_BUILD/$OBJ_NAME"

    done

    rm -f -- "$DEPS_PREFIX/lib/libdavs2.a"

    "$AR" rcs \
        "$DEPS_PREFIX/lib/libdavs2.a" \
        "$DAVS2_BUILD"/*.o

    "$RANLIB" \
        "$DEPS_PREFIX/lib/libdavs2.a"

    if [ ! -f "$DEPS_PREFIX/lib/libdavs2.a" ]; then
        echo "[ERROR] libdavs2.a was not generated"
        exit 1
    fi

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
    # 15. Build libuavs3d
    # ========================================================

    echo
    echo "-------------------------------------------------------"
    echo ">>> Building libuavs3d: $ARCH"
    echo "-------------------------------------------------------"
    echo

    pushd "$UAVS3D_BUILD" >/dev/null

    cmake "$UAVS3D_DIR" \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_CMAKE_TOOLCHAIN" \
        -DANDROID_ABI="$ARCH" \
        -DANDROID_PLATFORM="android-$API_LEVEL" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_AR="$AR" \
        -DCMAKE_RANLIB="$RANLIB" \
        -DCMAKE_STRIP="$STRIP" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DUAVS3D_TARGET_CPU="$UAVS3D_CPU" \
        -DCOMPILE_10BIT=ON

    make -j"$NPROC" uavs3d

    popd >/dev/null

    UAVS3D_LIB="$UAVS3D_BUILD/source/libuavs3d.a"

    if [ ! -f "$UAVS3D_LIB" ]; then
        echo "[ERROR] libuavs3d.a was not generated:"
        echo "        $UAVS3D_LIB"
        exit 1
    fi

    cp -f \
        "$UAVS3D_LIB" \
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
    # 16. Validate generated files
    # ========================================================

    echo
    echo "-------------------------------------------------------"
    echo ">>> Checking $ARCH"
    echo "-------------------------------------------------------"
    echo

    REQUIRED_FILES=(
        "$DEPS_PREFIX/lib/libdavs2.a"
        "$DEPS_PREFIX/lib/libuavs3d.a"
        "$DEPS_PREFIX/lib/pkgconfig/davs2.pc"
        "$DEPS_PREFIX/lib/pkgconfig/uavs3d.pc"
        "$DEPS_PREFIX/include/davs2.h"
        "$DEPS_PREFIX/include/davs2_config.h"
        "$DEPS_PREFIX/include/uavs3d.h"
    )

    for FILE in "${REQUIRED_FILES[@]}"; do

        if [ ! -f "$FILE" ]; then
            echo "[ERROR] Missing required file:"
            echo "        $FILE"
            exit 1
        fi

    done

    echo ">>> All required files exist."

    # ========================================================
    # 17. pkg-config validation
    # ========================================================

    export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig"
    export PKG_CONFIG_PATH=""

    if ! command -v pkg-config >/dev/null 2>&1; then
        echo "[ERROR] pkg-config not found"
        exit 1
    fi

    echo
    echo ">>> pkg-config validation"
    echo

    if ! pkg-config --exists "davs2 >= 1.6.0"; then
        echo "[ERROR] davs2 >= 1.6.0 not found"
        exit 1
    fi

    if ! pkg-config --exists "uavs3d >= 1.1.89"; then
        echo "[ERROR] uavs3d >= 1.1.89 not found"
        exit 1
    fi

    echo "davs2 version : $(pkg-config --modversion davs2)"
    echo "uavs3d version: $(pkg-config --modversion uavs3d)"

    echo
    echo ">>> davs2 Cflags:"
    pkg-config --cflags davs2

    echo
    echo ">>> davs2 Libs:"
    pkg-config --libs davs2

    echo
    echo ">>> uavs3d Cflags:"
    pkg-config --cflags uavs3d

    echo
    echo ">>> uavs3d Libs:"
    pkg-config --libs uavs3d

    unset PKG_CONFIG_LIBDIR
    unset PKG_CONFIG_PATH

    # ========================================================
    # 18. ABI complete
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> $ARCH build completed successfully"
    echo "======================================================="
    echo

done

# ============================================================
# 19. Final validation
# ============================================================

echo
echo "======================================================="
echo ">>> Final Android dependency validation"
echo "======================================================="
echo

for ARCH in "${SELECTED_ARCHS[@]}"; do

    PREFIX="$DEPS_ROOT/$ARCH"

    echo "[$ARCH]"

    if [ -f "$PREFIX/lib/libdavs2.a" ]; then
        echo "  OK  libdavs2.a"
    else
        echo "  ERROR libdavs2.a"
        exit 1
    fi

    if [ -f "$PREFIX/lib/libuavs3d.a" ]; then
        echo "  OK  libuavs3d.a"
    else
        echo "  ERROR libuavs3d.a"
        exit 1
    fi

    if [ -f "$PREFIX/lib/pkgconfig/davs2.pc" ]; then
        echo "  OK  davs2.pc"
    else
        echo "  ERROR davs2.pc"
        exit 1
    fi

    if [ -f "$PREFIX/lib/pkgconfig/uavs3d.pc" ]; then
        echo "  OK  uavs3d.pc"
    else
        echo "  ERROR uavs3d.pc"
        exit 1
    fi

    echo

done

echo "======================================================="
echo ">>> Android dependency build completed"
echo "======================================================="
echo
echo "Output:"
echo "  $DEPS_ROOT"
echo
echo "Intermediate:"
echo "  $DEPS_BUILD_ROOT"
echo