#!/bin/bash

set -euo pipefail

# ============================================================
# build_mpv_android.sh
#
# Build libmpv.so for Android using:
#   - project self-built FFmpeg 9.0.1
#   - project self-built libplacebo 7.360.1
#   - project self-built libass
#   - project-built Shaderc is consumed by libplacebo
#
# Target:
#   Android API/minSdk 26
#   arm64-v8a / armeabi-v7a / x86_64
#
# Renderer:
#   gpu-next / libplacebo
#   Vulkan + Android EGL + OpenGL ES
#
# Audio:
#   AAudio + AudioTrack + OpenSL ES
#
# Hardware/media:
#   Android Media NDK
#
# Output:
#   android_build/<ABI>/mpv/lib/libmpv.so
#   android_build/<ABI>/mpv/lib/libc++_shared.so
#   android_build/<ABI>/mpv/include/mpv/*.h
#
# IMPORTANT:
#   - Uses the project's FFmpeg/dependency pkg-config dirs only.
#   - Uses local/self-built libplacebo and libass.
#   - libplacebo is already built with Shaderc; mpv's own shaderc
#     option remains disabled on Android.
#   - No Meson --prefix is used.
#   - Uses DESTDIR staging for mpv install.
#   - Final libmpv.so is stripped with llvm-strip --strip-unneeded.
#   - NEEDED and SONAME are verified unchanged after stripping.
#   - Meson configure is run with --no-pager, so no '(END)' prompt.
#
# API 26 is intentional for this project. Therefore:
#   - Vulkan        enabled
#   - AAudio        enabled
#   - iconv         disabled
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
cd "$PROJECT_ROOT"

ENV_FILE="$PROJECT_ROOT/env.sh"
if [ ! -f "$ENV_FILE" ]; then
    echo "[ERROR] env.sh not found: $ENV_FILE"
    exit 1
fi
source "$ENV_FILE"

: "${PROJECT_ROOT:?PROJECT_ROOT is not set}"
: "${NDK_PATH:?NDK_PATH is not set}"
: "${API_LEVEL:?API_LEVEL is not set}"
: "${ANDROID_DEPS_ROOT:?ANDROID_DEPS_ROOT is not set}"
: "${BUILD_DIR_ROOT:?BUILD_DIR_ROOT is not set}"
: "${OPT_FLAGS:?OPT_FLAGS is not set}"

# ------------------------------------------------------------
# Standardize API level for this build
# ------------------------------------------------------------
if [ "$API_LEVEL" -ne 26 ]; then
    echo
    echo "[ERROR] This mpv Android build is standardized on API_LEVEL=26."
    echo "        Current API_LEVEL=$API_LEVEL"
    echo "        Your App minSdk is 26."
    echo "        Set API_LEVEL=26 in build_scripts/env.sh."
    exit 1
fi

# ------------------------------------------------------------
# Source / roots
# ------------------------------------------------------------
MPV_DIR="$PROJECT_ROOT/mpv"
DEPS_ROOT="$ANDROID_DEPS_ROOT"
FFMPEG_ROOT="$BUILD_DIR_ROOT"
MPV_BUILD_ROOT="$PROJECT_ROOT/android_build_deps"
LIBMPV_ANDROID_ROOT="${LIBMPV_ANDROID_ROOT:-$PROJECT_ROOT/libmpv-android}"
LIBPLAYER_SRC="$LIBMPV_ANDROID_ROOT/libmpv/src/main/cpp"

if [ ! -f "$MPV_DIR/meson.build" ]; then
    echo "[ERROR] mpv meson.build not found:"
    echo "        $MPV_DIR/meson.build"
    exit 1
fi

if [ ! -f "$MPV_DIR/meson.options" ] && [ ! -f "$MPV_DIR/meson_options.txt" ]; then
    echo "[ERROR] mpv Meson options file not found."
    exit 1
fi

# ------------------------------------------------------------
# libplayer JNI bridge source
# ------------------------------------------------------------
if [ ! -d "$LIBPLAYER_SRC" ]; then
    echo "[ERROR] libplayer JNI source directory not found:"
    echo "        $LIBPLAYER_SRC"
    echo "        Set LIBMPV_ANDROID_ROOT if your source is elsewhere."
    exit 1
fi

LIBPLAYER_SOURCES=(
    main.cpp
    render.cpp
    log.cpp
    jni_utils.cpp
    property.cpp
    event.cpp
)

for SRC in "${LIBPLAYER_SOURCES[@]}"; do
    if [ ! -f "$LIBPLAYER_SRC/$SRC" ]; then
        echo "[ERROR] libplayer source file not found:"
        echo "        $LIBPLAYER_SRC/$SRC"
        exit 1
    fi
done

if [ ! -f "$LIBPLAYER_SRC/CMakeLists.txt" ]; then
    echo "[ERROR] libplayer CMakeLists.txt not found:"
    echo "        $LIBPLAYER_SRC/CMakeLists.txt"
    exit 1
fi

# ------------------------------------------------------------
# NDK
# ------------------------------------------------------------
HOST_TAG="windows-x86_64"
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG"
LLVM_BIN="$TOOLCHAIN/bin"
SYSROOT="$TOOLCHAIN/sysroot"
ANDROID_CMAKE_TOOLCHAIN="$NDK_PATH/build/cmake/android.toolchain.cmake"

if [ ! -d "$TOOLCHAIN" ]; then
    echo "[ERROR] Android NDK toolchain not found: $TOOLCHAIN"
    exit 1
fi

export PATH="/mingw64/bin:/usr/local/bin:/usr/bin:/bin:$LLVM_BIN"

MESON_BIN="/mingw64/bin/meson"
NINJA_BIN="/mingw64/bin/ninja"
CMAKE_BIN="$(command -v cmake || true)"
PKG_CONFIG_EXE="$(command -v pkg-config || true)"
PYTHON_BIN="$(command -v python3 || command -v python || true)"

for TOOL in \
    "$MESON_BIN" \
    "$NINJA_BIN" \
    "$LLVM_BIN/clang.exe" \
    "$LLVM_BIN/clang++.exe" \
    "$LLVM_BIN/llvm-ar.exe" \
    "$LLVM_BIN/llvm-ranlib.exe" \
    "$LLVM_BIN/llvm-strip.exe" \
    "$LLVM_BIN/llvm-readelf.exe"
do
    if [ ! -f "$TOOL" ]; then
        echo "[ERROR] Required tool not found:"
        echo "        $TOOL"
        exit 1
    fi
done

if [ -z "$CMAKE_BIN" ] || [ ! -f "$CMAKE_BIN" ]; then
    echo "[ERROR] CMake not found."
    exit 1
fi

if [ -z "$PKG_CONFIG_EXE" ]; then
    echo "[ERROR] pkg-config not found."
    exit 1
fi

if [ -z "$PYTHON_BIN" ]; then
    echo "[ERROR] Python 3 not found."
    exit 1
fi

NPROC="$(nproc 2>/dev/null || echo 4)"

# ------------------------------------------------------------
# ABI selector
# ------------------------------------------------------------
select_archs() {
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
    echo "MPV Android ABI Selector"
    echo "======================================================="
    echo "  1) arm64-v8a"
    echo "  2) armeabi-v7a"
    echo "  3) x86_64"
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

select_archs "$@"

# ------------------------------------------------------------
# Global summary
# ------------------------------------------------------------
echo
echo "======================================================="
echo ">>> MPV Android build environment"
echo "======================================================="
echo "    PROJECT_ROOT           = $PROJECT_ROOT"
echo "    MPV_DIR                = $MPV_DIR"
echo "    NDK_PATH               = $NDK_PATH"
echo "    API_LEVEL              = $API_LEVEL"
echo "    DEPS_ROOT              = $DEPS_ROOT"
echo "    FFMPEG_ROOT            = $FFMPEG_ROOT"
echo "    MESON                  = $MESON_BIN"
echo "    NINJA                  = $NINJA_BIN"
echo "    NPROC                  = $NPROC"
echo
echo "    Renderer:"
echo "      libplacebo           = required dependency"
echo "      Vulkan               = enabled"
echo "      OpenGL               = enabled"
echo "      EGL                  = disabled"
echo "      EGL Android          = enabled"
echo "      plain-gl             = enabled"
echo
echo "    Android media/audio:"
echo "      Media NDK            = enabled"
echo "      AAudio               = enabled"
echo "      AudioTrack           = enabled"
echo "      OpenSL ES            = enabled"
echo
echo "    Dependencies:"
echo "      libass               = required dependency"
echo "      FFmpeg               = self-built"
echo "      iconv                = disabled (API 26)"
echo "      mpv shaderc          = disabled"
echo "      spirv-cross          = disabled"
echo
echo "    Optimization:"
echo "      release              = yes"
echo "      O3                   = yes"
echo "      LTO                  = yes"
echo "      strip                = --strip-unneeded"
echo "======================================================="
echo

# ------------------------------------------------------------
# Build each ABI
# ------------------------------------------------------------
for ARCH in "${SELECTED_ARCHS[@]}"; do
    echo
    echo
    echo "======================================================="
    echo ">>> Building MPV [$ARCH]"
    echo "======================================================="

    case "$ARCH" in
        arm64-v8a)
            CLANG_TRIPLE="aarch64-linux-android${API_LEVEL}"
            MESON_CPU_FAMILY="aarch64"
            MESON_CPU="arm64"
            LIBCXX_ABI_DIR="aarch64-linux-android"
            ;;
        armeabi-v7a)
            CLANG_TRIPLE="armv7a-linux-androideabi${API_LEVEL}"
            MESON_CPU_FAMILY="arm"
            MESON_CPU="armv7-a"
            LIBCXX_ABI_DIR="arm-linux-androideabi"
            ;;
        x86_64)
            CLANG_TRIPLE="x86_64-linux-android${API_LEVEL}"
            MESON_CPU_FAMILY="x86_64"
            MESON_CPU="x86_64"
            LIBCXX_ABI_DIR="x86_64-linux-android"
            ;;
    esac

    DEPS_PREFIX="$DEPS_ROOT/$ARCH"
    FFMPEG_PREFIX="$FFMPEG_ROOT/$ARCH"
    MPV_PREFIX="$FFMPEG_PREFIX/mpv"
    MPV_BUILD="$MPV_BUILD_ROOT/$ARCH/mpv"
    MPV_STAGE="$MPV_BUILD/stage"
    CROSS_FILE="$MPV_BUILD/cross_file.txt"

    # libplayer JNI bridge build directory / output
    LIBPLAYER_BUILD="$MPV_BUILD_ROOT/$ARCH/libplayer"
    LIBPLAYER_PREFIX="$MPV_PREFIX"
    LIBPLAYER_CMAKE_BUILD="$LIBPLAYER_BUILD/build"
    LIBPLAYER_CMAKE_FILE="$LIBPLAYER_BUILD/CMakeLists.txt"
    LIBPLAYER_LIB="$LIBPLAYER_PREFIX/lib/libplayer.so"

    # --------------------------------------------------------
    # Verify FFmpeg
    # --------------------------------------------------------
    REQUIRED_FFMPEG_LIBS=(
        libavcodec.so
        libavformat.so
        libavutil.so
        libavfilter.so
        libswresample.so
        libswscale.so
        libavdevice.so
    )

    for LIB in "${REQUIRED_FFMPEG_LIBS[@]}"; do
        if [ ! -f "$FFMPEG_PREFIX/lib/$LIB" ]; then
            echo
            echo "[ERROR] Missing FFmpeg library:"
            echo "        $FFMPEG_PREFIX/lib/$LIB"
            exit 1
        fi
    done

    if [ ! -d "$FFMPEG_PREFIX/lib/pkgconfig" ]; then
        echo "[ERROR] FFmpeg pkg-config directory not found:"
        echo "        $FFMPEG_PREFIX/lib/pkgconfig"
        exit 1
    fi

    # --------------------------------------------------------
    # Verify libplacebo / libass
    # --------------------------------------------------------
    REQUIRED_DEPS=(
        "$DEPS_PREFIX/lib/libplacebo.a"
        "$DEPS_PREFIX/lib/libass.a"
    )

    for FILE in "${REQUIRED_DEPS[@]}"; do
        if [ ! -f "$FILE" ]; then
            echo
            echo "[ERROR] Required dependency not found:"
            echo "        $FILE"
            exit 1
        fi
    done

    REQUIRED_PC_FILES=(
        "$DEPS_PREFIX/lib/pkgconfig/libplacebo.pc"
        "$DEPS_PREFIX/lib/pkgconfig/vulkan.pc"
        "$DEPS_PREFIX/lib/pkgconfig/libass.pc"
    )

    for FILE in "${REQUIRED_PC_FILES[@]}"; do
        if [ ! -f "$FILE" ]; then
            echo
            echo "[ERROR] Required pkg-config file not found:"
            echo "        $FILE"
            exit 1
        fi
    done

    # --------------------------------------------------------
    # NDK tools
    # --------------------------------------------------------
    CLANG_EXE="$LLVM_BIN/clang.exe"
    CLANGXX_EXE="$LLVM_BIN/clang++.exe"
    AR="$LLVM_BIN/llvm-ar.exe"
    NM="$LLVM_BIN/llvm-nm.exe"
    RANLIB="$LLVM_BIN/llvm-ranlib.exe"
    STRIP="$LLVM_BIN/llvm-strip.exe"
    READELF="$LLVM_BIN/llvm-readelf.exe"

    CLANG_EXE_WIN="$(cygpath -m "$CLANG_EXE")"
    CLANGXX_EXE_WIN="$(cygpath -m "$CLANGXX_EXE")"
    AR_WIN="$(cygpath -m "$AR")"
    NM_WIN="$(cygpath -m "$NM")"
    RANLIB_WIN="$(cygpath -m "$RANLIB")"
    STRIP_WIN="$(cygpath -m "$STRIP")"
    PKG_CONFIG_WIN="$(cygpath -m "$PKG_CONFIG_EXE")"
    PYTHON_WIN="$(cygpath -m "$PYTHON_BIN")"

    MPV_DIR_WIN="$(cygpath -m "$MPV_DIR")"
    MPV_BUILD_WIN="$(cygpath -m "$MPV_BUILD")"
	MPV_PREFIX_WIN="$(cygpath -m "$MPV_PREFIX")"
    CROSS_FILE_WIN="$(cygpath -m "$CROSS_FILE")"
    ANDROID_CMAKE_TOOLCHAIN_WIN="$(cygpath -m "$ANDROID_CMAKE_TOOLCHAIN")"
    LIBPLAYER_SRC_WIN="$(cygpath -m "$LIBPLAYER_SRC")"
    LIBPLAYER_BUILD_WIN="$(cygpath -m "$LIBPLAYER_BUILD")"
    LIBPLAYER_CMAKE_BUILD_WIN="$(cygpath -m "$LIBPLAYER_CMAKE_BUILD")"
    LIBPLAYER_CMAKE_FILE_WIN="$(cygpath -m "$LIBPLAYER_CMAKE_FILE")"
    LIBPLAYER_PREFIX_WIN="$(cygpath -m "$LIBPLAYER_PREFIX")"

    DEPS_PREFIX_WIN="$(cygpath -m "$DEPS_PREFIX")"
    FFMPEG_PREFIX_WIN="$(cygpath -m "$FFMPEG_PREFIX")"

    # --------------------------------------------------------
    # pkg-config isolation
    # --------------------------------------------------------
    export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig:$FFMPEG_PREFIX/lib/pkgconfig"
    export PKG_CONFIG_PATH=""
    export PKG_CONFIG_SYSTEM_LIBRARY_PATH=""
    export PKG_CONFIG_SYSTEM_INCLUDE_PATH=""
    unset PKG_CONFIG_SYSROOT_DIR 2>/dev/null || true

    echo
    echo "======================================================="
    echo ">>> pkg-config verification [$ARCH]"
    echo "======================================================="
    echo "    PKG_CONFIG_LIBDIR=$PKG_CONFIG_LIBDIR"

    REQUIRED_PC_MODULES=(
        libavcodec
        libavformat
        libavutil
        libavfilter
        libswresample
        libswscale
        libavdevice
        libplacebo
        libass
        vulkan
    )

    for MODULE in "${REQUIRED_PC_MODULES[@]}"; do
        if ! pkg-config --exists "$MODULE"; then
            echo "[ERROR] Missing pkg-config module: $MODULE"
            exit 1
        fi
        echo "[OK] $MODULE = $(pkg-config --modversion "$MODULE")"
    done

    # --------------------------------------------------------
    # Cross file
    # --------------------------------------------------------
    rm -rf -- "$MPV_BUILD"
    mkdir -p -- "$MPV_BUILD"

    cat > "$CROSS_FILE" <<EOF_CROSS
[binaries]
c = '$CLANG_EXE_WIN'
cpp = '$CLANGXX_EXE_WIN'
ar = '$AR_WIN'
nm = '$NM_WIN'
ranlib = '$RANLIB_WIN'
strip = '$STRIP_WIN'
pkg-config = '$PKG_CONFIG_WIN'
python = '$PYTHON_WIN'

[host_machine]
system = 'android'
cpu_family = '$MESON_CPU_FAMILY'
cpu = '$MESON_CPU'
endian = 'little'

[built-in options]
c_args = ['--target=$CLANG_TRIPLE', '-fPIC', '-O3']
cpp_args = ['--target=$CLANG_TRIPLE', '-fPIC', '-O3']
c_link_args = ['--target=$CLANG_TRIPLE']
cpp_link_args = ['--target=$CLANG_TRIPLE']

[properties]
needs_exe_wrapper = true
pkg_config_libdir = ['$DEPS_PREFIX_WIN/lib/pkgconfig', '$FFMPEG_PREFIX_WIN/lib/pkgconfig']
EOF_CROSS

    # --------------------------------------------------------
    # MPV Meson configuration
    # --------------------------------------------------------
    # Important for current mpv:
    #   - libass/libplacebo are dependencies, not Meson options.
    #   - mpv's shaderc feature is disabled on Android.
    #   - generic EGL is disabled; Android EGL is enabled.
    #   - iconv is disabled for API 26.
    # --------------------------------------------------------
    echo
    echo "======================================================="
    echo ">>> MPV Meson configuration [$ARCH]"
    echo "======================================================="

    "$MESON_BIN" setup \
        "$MPV_BUILD_WIN" \
        "$MPV_DIR_WIN" \
        --cross-file "$CROSS_FILE_WIN" \
        --libdir lib \
        --default-library shared \
        --buildtype release \
        -Db_lto=true \
        -Doptimization=3 \
        -Dlibmpv=true \
        -Dcplayer=false \
        -Dbuild-date=true \
        -Dtests=false \
        -Dfuzzers=false \
        -Dcplugins=disabled \
        -Dlua=disabled \
        -Ddvbin=disabled \
        -Ddvda=disabled \
        -Ddvdnav=disabled \
        -Dcdda=disabled \
        -Djavascript=disabled \
        -Djpeg=disabled \
        -Dlcms2=disabled \
        -Dlibarchive=disabled \
        -Dlibbluray=disabled \
        -Dlibcurl=disabled \
        -Drubberband=disabled \
        -Dsubrandr=disabled \
        -Duchardet=disabled \
        -Dvapoursynth=disabled \
        -Dzimg=disabled \
        -Dalsa=disabled \
        -Djack=disabled \
        -Dpipewire=disabled \
        -Dpulse=disabled \
        -Dsndio=disabled \
        -Dwasapi=disabled \
        -Doss-audio=disabled \
        -Dopenal=disabled \
        -Daudiotrack=enabled \
        -Daaudio=enabled \
        -Dopensles=enabled \
        -Dgl=enabled \
        -Degl=disabled \
        -Degl-android=enabled \
        -Dplain-gl=enabled \
        -Dvulkan=enabled \
        -Dandroid-media-ndk=enabled \
        -Dd3d11=disabled \
        -Ddirect3d=disabled \
        -Ddmabuf-wayland=disabled \
        -Ddrm=disabled \
        -Dgbm=disabled \
        -Degl-angle=disabled \
        -Degl-angle-lib=disabled \
        -Degl-angle-win32=disabled \
        -Degl-drm=disabled \
        -Degl-wayland=disabled \
        -Degl-x11=disabled \
        -Dgl-cocoa=disabled \
        -Dgl-dxinterop=disabled \
        -Dgl-win32=disabled \
        -Dgl-x11=disabled \
        -Dsdl2-video=disabled \
        -Dvdpau=disabled \
        -Dvaapi=disabled \
        -Dvaapi-drm=disabled \
        -Dvaapi-wayland=disabled \
        -Dvaapi-x11=disabled \
        -Dwayland=disabled \
        -Dx11=disabled \
        -Dxv=disabled \
        -Dshaderc=disabled \
        -Dspirv-cross=disabled \
        -Dlibavdevice=enabled \
        -Diconv=disabled \
        -Dmanpage-build=disabled \
        -Dhtml-build=disabled \
        -Dpdf-build=disabled

    # --------------------------------------------------------
    # Show only relevant configuration; never invoke a pager.
    # --------------------------------------------------------
    echo
    echo "======================================================="
    echo ">>> MPV important configuration [$ARCH]"
    echo "======================================================="

    "$MESON_BIN" configure --no-pager "$MPV_BUILD_WIN" \
        | grep -Ei \
            '^  (aaudio|android-media-ndk|audiotrack|cplayer|egl |egl-android|gl |iconv|libavdevice|libmpv|opensles|plain-gl|shaderc|spirv-cross|vulkan|zlib)[[:space:]]' \
        || true

    # --------------------------------------------------------
    # Build
    # --------------------------------------------------------
    echo
    echo "======================================================="
    echo ">>> Building MPV [$ARCH]"
    echo "======================================================="

    "$NINJA_BIN" \
        -C "$MPV_BUILD_WIN" \
        -j"$NPROC"

    # --------------------------------------------------------
    # Stage install
    # --------------------------------------------------------
    echo
    echo "======================================================="
    echo ">>> Staging MPV install [$ARCH]"
    echo "======================================================="

    rm -rf -- "$MPV_STAGE"
    mkdir -p -- "$MPV_STAGE"

    DESTDIR="$(cygpath -m "$MPV_STAGE")" \
        "$NINJA_BIN" \
        -C "$MPV_BUILD_WIN" \
        install

    # --------------------------------------------------------
    # Locate staged libmpv
    # --------------------------------------------------------
    STAGE_PREFIX="$MPV_STAGE/usr/local"
    LIBMPV_STAGE=""

    if [ -f "$STAGE_PREFIX/lib/libmpv.so" ]; then
        LIBMPV_STAGE="$STAGE_PREFIX/lib/libmpv.so"
    else
        LIBMPV_STAGE="$(find "$MPV_STAGE" -type f -name 'libmpv.so' -print -quit)"
    fi

    if [ -z "$LIBMPV_STAGE" ] || [ ! -f "$LIBMPV_STAGE" ]; then
        echo "[ERROR] Staged libmpv.so not found."
        echo "        Stage: $MPV_STAGE"
        exit 1
    fi

    # --------------------------------------------------------
    # Locate public headers
    # --------------------------------------------------------
    STAGE_MPV_HEADERS="$STAGE_PREFIX/include/mpv"
    if [ ! -d "$STAGE_MPV_HEADERS" ]; then
        STAGE_MPV_HEADERS="$(find "$MPV_STAGE" -type d -path '*/include/mpv' -print -quit)"
    fi

    if [ -z "$STAGE_MPV_HEADERS" ] || [ ! -d "$STAGE_MPV_HEADERS" ]; then
        if [ -d "$MPV_DIR/include/mpv" ]; then
            STAGE_MPV_HEADERS="$MPV_DIR/include/mpv"
        else
            echo "[ERROR] mpv public headers not found."
            exit 1
        fi
    fi

    # --------------------------------------------------------
    # Final app-facing output
    # --------------------------------------------------------
    rm -rf -- "$MPV_PREFIX"
    mkdir -p -- "$MPV_PREFIX/lib" "$MPV_PREFIX/include/mpv"

    cp -f "$LIBMPV_STAGE" "$MPV_PREFIX/lib/libmpv.so"
    cp -a "$STAGE_MPV_HEADERS/." "$MPV_PREFIX/include/mpv/"

    # --------------------------------------------------------
    # Copy libc++_shared.so from NDK
    # --------------------------------------------------------
    LIBCXX_SHARED="$SYSROOT/usr/lib/$LIBCXX_ABI_DIR/libc++_shared.so"

    if [ ! -f "$LIBCXX_SHARED" ]; then
        echo
        echo "[ERROR] libc++_shared.so not found:"
        echo "        $LIBCXX_SHARED"
        exit 1
    fi

    cp -f \
        "$LIBCXX_SHARED" \
        "$MPV_PREFIX/lib/libc++_shared.so"

    # --------------------------------------------------------
    # Verify architecture before stripping
    # --------------------------------------------------------
    echo
    echo "======================================================="
    echo ">>> libmpv.so verification before strip [$ARCH]"
    echo "======================================================="

    "$READELF" -h "$MPV_PREFIX/lib/libmpv.so" \
        | grep -E 'Class:|Machine:|Type:' \
        || true

    echo
    echo ">>> NEEDED before strip:"
    "$READELF" -d "$MPV_PREFIX/lib/libmpv.so" \
        | grep 'NEEDED' \
        || true

    echo
    echo ">>> SONAME before strip:"
    "$READELF" -d "$MPV_PREFIX/lib/libmpv.so" \
        | grep 'SONAME' \
        || true

    # --------------------------------------------------------
    # Automatic size reduction
    #
    # We have already manually verified on arm64-v8a that:
    #   ~17 MB -> ~13 MB
    # and NEEDED dependencies remain unchanged.
    #
    # Keep a temporary backup only until post-strip verification passes.
    # --------------------------------------------------------
    LIBMPV="$MPV_PREFIX/lib/libmpv.so"
    LIBMPV_UNSTRIPPED_TMP="$MPV_BUILD/libmpv.unstripped.tmp.so"
    NEEDED_BEFORE="$MPV_BUILD/libmpv.needed.before.txt"
    SONAME_BEFORE="$MPV_BUILD/libmpv.soname.before.txt"
    NEEDED_AFTER="$MPV_BUILD/libmpv.needed.after.txt"
    SONAME_AFTER="$MPV_BUILD/libmpv.soname.after.txt"

    rm -f \
        "$LIBMPV_UNSTRIPPED_TMP" \
        "$NEEDED_BEFORE" \
        "$SONAME_BEFORE" \
        "$NEEDED_AFTER" \
        "$SONAME_AFTER"

    cp -f "$LIBMPV" "$LIBMPV_UNSTRIPPED_TMP"

    "$READELF" -d "$LIBMPV" \
        | grep 'NEEDED' \
        > "$NEEDED_BEFORE"

    "$READELF" -d "$LIBMPV" \
        | grep 'SONAME' \
        > "$SONAME_BEFORE"

    SIZE_BEFORE="$(stat -c '%s' "$LIBMPV")"

    echo
    echo ">>> Stripping libmpv.so"
    echo "    method = llvm-strip --strip-unneeded"
    echo "    before = $SIZE_BEFORE bytes"

    "$STRIP" \
        --strip-unneeded \
        "$LIBMPV"

    "$READELF" -d "$LIBMPV" \
        | grep 'NEEDED' \
        > "$NEEDED_AFTER"

    "$READELF" -d "$LIBMPV" \
        | grep 'SONAME' \
        > "$SONAME_AFTER"

    # --------------------------------------------------------
    # Verify stripping did not change runtime dependencies
    # --------------------------------------------------------
    if ! cmp -s "$NEEDED_BEFORE" "$NEEDED_AFTER"; then
        echo
        echo "[ERROR] libmpv.so NEEDED dependencies changed after strip."
        echo "        Restoring unstripped library."
        cp -f "$LIBMPV_UNSTRIPPED_TMP" "$LIBMPV"
        exit 1
    fi

    if ! cmp -s "$SONAME_BEFORE" "$SONAME_AFTER"; then
        echo
        echo "[ERROR] libmpv.so SONAME changed after strip."
        echo "        Restoring unstripped library."
        cp -f "$LIBMPV_UNSTRIPPED_TMP" "$LIBMPV"
        exit 1
    fi

    # --------------------------------------------------------
    # Verify debug/symbol sections are removed
    # --------------------------------------------------------
    if "$READELF" -S "$LIBMPV" \
        | grep -Eq '\.debug_|[[:space:]]\.symtab[[:space:]]'; then
        echo
        echo "[ERROR] Debug/symtab sections still remain after strip."
        echo "        Restoring unstripped library."
        cp -f "$LIBMPV_UNSTRIPPED_TMP" "$LIBMPV"
        exit 1
    fi

    SIZE_AFTER="$(stat -c '%s' "$LIBMPV")"

    rm -f \
        "$LIBMPV_UNSTRIPPED_TMP" \
        "$NEEDED_BEFORE" \
        "$SONAME_BEFORE" \
        "$NEEDED_AFTER" \
        "$SONAME_AFTER"

    echo "    after  = $SIZE_AFTER bytes"

    # --------------------------------------------------------
    # Final runtime dependency report
    # --------------------------------------------------------
    echo
    echo "======================================================="
    echo ">>> Final libmpv.so runtime dependencies [$ARCH]"
    echo "======================================================="

    "$READELF" -d "$LIBMPV" \
        | grep 'NEEDED' \
        || true

    echo
    echo ">>> Final SONAME:"
    "$READELF" -d "$LIBMPV" \
        | grep 'SONAME' \
        || true

    # ========================================================
    # Build libplayer JNI bridge against the just-built libmpv
    # and the same ABI FFmpeg libavcodec.so.
    # ========================================================
    echo
    echo "======================================================="
    echo ">>> Building libplayer JNI bridge [$ARCH]"
    echo "======================================================="

    rm -rf -- "$LIBPLAYER_BUILD"
    mkdir -p -- "$LIBPLAYER_CMAKE_BUILD"

    # Generate a self-contained CMake project instead of using the
    # original CMakeLists paths, which point to the old AAR project
    # layout (buildscripts/prefix and bundled jniLibs).
    cat > "$LIBPLAYER_CMAKE_FILE" <<EOF_LIBPLAYER
cmake_minimum_required(VERSION 3.22.1)

project(libplayer LANGUAGES C CXX)

add_library(player SHARED
    "$LIBPLAYER_SRC_WIN/main.cpp"
    "$LIBPLAYER_SRC_WIN/render.cpp"
    "$LIBPLAYER_SRC_WIN/log.cpp"
    "$LIBPLAYER_SRC_WIN/jni_utils.cpp"
    "$LIBPLAYER_SRC_WIN/property.cpp"
    "$LIBPLAYER_SRC_WIN/event.cpp"
)

add_library(mpv SHARED IMPORTED)
set_target_properties(mpv PROPERTIES
    IMPORTED_LOCATION "$MPV_PREFIX_WIN/lib/libmpv.so"
    INTERFACE_INCLUDE_DIRECTORIES "$MPV_PREFIX_WIN/include"
)

add_library(avcodec SHARED IMPORTED)
set_target_properties(avcodec PROPERTIES
    IMPORTED_LOCATION "$FFMPEG_PREFIX_WIN/lib/libavcodec.so"
    INTERFACE_INCLUDE_DIRECTORIES "$FFMPEG_PREFIX_WIN/include"
)

# libplayer sources include both mpv/client.h and FFmpeg headers.
target_include_directories(player PRIVATE
    "$LIBPLAYER_SRC_WIN"
    "$MPV_PREFIX_WIN/include"
    "$FFMPEG_PREFIX_WIN/include"
)

# Keep JNI bridge compatible with the existing libmpv-android project.
target_compile_features(player PRIVATE cxx_std_17)

target_link_options(player PRIVATE
    "-Wl,--hash-style=both"
)

target_link_libraries(player PRIVATE
    mpv
    avcodec
    log
)

set_target_properties(player PROPERTIES
    OUTPUT_NAME player
    POSITION_INDEPENDENT_CODE ON
)
EOF_LIBPLAYER

    # CMake will use the Android NDK and its libc++_shared runtime.
    # The imported libmpv/libavcodec are the exact outputs of this ABI.
    "$CMAKE_BIN" \
        -S "$LIBPLAYER_BUILD_WIN" \
        -B "$LIBPLAYER_CMAKE_BUILD_WIN" \
        -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_CMAKE_TOOLCHAIN_WIN" \
        -DANDROID_ABI="$ARCH" \
        -DANDROID_PLATFORM="android-$API_LEVEL" \
        -DANDROID_STL=c++_shared \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_CXX_STANDARD_REQUIRED=ON \
        -DCMAKE_C_FLAGS="$OPT_FLAGS -fPIC" \
        -DCMAKE_CXX_FLAGS="$OPT_FLAGS -fPIC" \
        -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--hash-style=both"

    "$NINJA_BIN" -C "$LIBPLAYER_CMAKE_BUILD_WIN" -j"$NPROC"

    LIBPLAYER_BUILT="$(find "$LIBPLAYER_CMAKE_BUILD" -type f -name 'libplayer.so' -print -quit)"

    if [ -z "$LIBPLAYER_BUILT" ] || [ ! -f "$LIBPLAYER_BUILT" ]; then
        echo
        echo "[ERROR] libplayer.so was not generated."
        echo "        Build directory: $LIBPLAYER_CMAKE_BUILD"
        exit 1
    fi

    cp -f "$LIBPLAYER_BUILT" "$LIBPLAYER_LIB"

    # --------------------------------------------------------
    # Strip libplayer.so
    # --------------------------------------------------------
    LIBPLAYER_UNSTRIPPED_TMP="$LIBPLAYER_BUILD/libplayer.unstripped.tmp.so"
    LP_NEEDED_BEFORE="$LIBPLAYER_BUILD/libplayer.needed.before.txt"
    LP_NEEDED_AFTER="$LIBPLAYER_BUILD/libplayer.needed.after.txt"
    LP_SONAME_BEFORE="$LIBPLAYER_BUILD/libplayer.soname.before.txt"
    LP_SONAME_AFTER="$LIBPLAYER_BUILD/libplayer.soname.after.txt"

    rm -f -- \
        "$LIBPLAYER_UNSTRIPPED_TMP" \
        "$LP_NEEDED_BEFORE" \
        "$LP_NEEDED_AFTER" \
        "$LP_SONAME_BEFORE" \
        "$LP_SONAME_AFTER"

    cp -f "$LIBPLAYER_LIB" "$LIBPLAYER_UNSTRIPPED_TMP"

    "$READELF" -d "$LIBPLAYER_LIB" | grep 'NEEDED' > "$LP_NEEDED_BEFORE"
    "$READELF" -d "$LIBPLAYER_LIB" | grep 'SONAME' > "$LP_SONAME_BEFORE"

    LP_SIZE_BEFORE="$(stat -c '%s' "$LIBPLAYER_LIB")"

    echo
    echo ">>> Stripping libplayer.so"
    echo "    method = llvm-strip --strip-unneeded"
    echo "    before = $LP_SIZE_BEFORE bytes"

    "$STRIP" --strip-unneeded "$LIBPLAYER_LIB"

    "$READELF" -d "$LIBPLAYER_LIB" | grep 'NEEDED' > "$LP_NEEDED_AFTER"
    "$READELF" -d "$LIBPLAYER_LIB" | grep 'SONAME' > "$LP_SONAME_AFTER"

    if ! cmp -s "$LP_NEEDED_BEFORE" "$LP_NEEDED_AFTER"; then
        echo "[ERROR] libplayer.so NEEDED changed after strip."
        cp -f "$LIBPLAYER_UNSTRIPPED_TMP" "$LIBPLAYER_LIB"
        exit 1
    fi

    if ! cmp -s "$LP_SONAME_BEFORE" "$LP_SONAME_AFTER"; then
        echo "[ERROR] libplayer.so SONAME changed after strip."
        cp -f "$LIBPLAYER_UNSTRIPPED_TMP" "$LIBPLAYER_LIB"
        exit 1
    fi

    if "$READELF" -S "$LIBPLAYER_LIB" | grep -Eq '\.debug_|[[:space:]]\.symtab[[:space:]]'; then
        echo "[ERROR] Debug/symtab sections remain in libplayer.so after strip."
        cp -f "$LIBPLAYER_UNSTRIPPED_TMP" "$LIBPLAYER_LIB"
        exit 1
    fi

    LP_SIZE_AFTER="$(stat -c '%s' "$LIBPLAYER_LIB")"

    # --------------------------------------------------------
    # Verify libplayer runtime dependencies and FFmpeg ABI version
    # --------------------------------------------------------
    echo
    echo ">>> libplayer.so NEEDED:"
    "$READELF" -d "$LIBPLAYER_LIB" | grep 'NEEDED' || true

    echo
    echo ">>> libplayer.so SONAME:"
    "$READELF" -d "$LIBPLAYER_LIB" | grep 'SONAME' || true

    echo
    echo ">>> av_jni_set_java_vm in libplayer.so:"
    "$LLVM_BIN/llvm-nm.exe" -D "$LIBPLAYER_LIB" \
        | grep 'av_jni_set_java_vm' \
        || true

    echo
    echo ">>> Required current FFmpeg symbol:"
    "$LLVM_BIN/llvm-nm.exe" -D "$FFMPEG_PREFIX/lib/libavcodec.so" \
        | grep 'av_jni_set_java_vm' \
        || true

    LP_SYMBOL_VERSION="$(
        "$LLVM_BIN/llvm-nm.exe" -D "$LIBPLAYER_LIB" 2>/dev/null \
            | grep 'av_jni_set_java_vm' \
            | head -1 \
            | sed -n 's/.*av_jni_set_java_vm@\(LIBAVCODEC_[0-9][0-9]*\).*/\1/p'
    )"

    if [ -n "$LP_SYMBOL_VERSION" ] && [ "$LP_SYMBOL_VERSION" != "LIBAVCODEC_63" ]; then
        echo
        echo "[ERROR] libplayer.so is linked against unexpected FFmpeg symbol version:"
        echo "        $LP_SYMBOL_VERSION"
        echo "        Expected: LIBAVCODEC_63"
        exit 1
    fi

    if ! "$READELF" -d "$LIBPLAYER_LIB" | grep -q 'Shared library: \[libmpv.so\]'; then
        echo "[ERROR] libplayer.so does not have NEEDED libmpv.so."
        exit 1
    fi

    if ! "$READELF" -d "$LIBPLAYER_LIB" | grep -q 'Shared library: \[libavcodec.so\]'; then
        echo "[ERROR] libplayer.so does not have NEEDED libavcodec.so."
        exit 1
    fi

    rm -f -- \
        "$LIBPLAYER_UNSTRIPPED_TMP" \
        "$LP_NEEDED_BEFORE" \
        "$LP_NEEDED_AFTER" \
        "$LP_SONAME_BEFORE" \
        "$LP_SONAME_AFTER"

    echo
    echo "    libplayer.so size before strip:"
    echo "      $LP_SIZE_BEFORE bytes"
    echo
    echo "    libplayer.so size after strip:"
    echo "      $LP_SIZE_AFTER bytes"

    # --------------------------------------------------------
    # Final important feature summary
    # --------------------------------------------------------
    echo
    echo "======================================================="
    echo ">>> Final MPV configuration [$ARCH]"
    echo "======================================================="

    "$MESON_BIN" configure --no-pager "$MPV_BUILD_WIN" \
        | grep -Ei \
            '^  (aaudio|android-media-ndk|audiotrack|cplayer|egl |egl-android|gl |iconv|libavdevice|libmpv|opensles|plain-gl|shaderc|spirv-cross|vulkan|zlib)[[:space:]]' \
        || true

    echo
    echo "======================================================="
    echo ">>> MPV [$ARCH] completed successfully"
    echo "======================================================="
    echo
    echo "    libmpv.so:"
    echo "      $LIBMPV"
    echo
    echo "    libc++_shared.so:"
    echo "      $MPV_PREFIX/lib/libc++_shared.so"
    echo
    echo "    libplayer.so:"
    echo "      $LIBPLAYER_LIB"
    echo
    echo "    headers:"
    echo "      $MPV_PREFIX/include/mpv"
    echo
    echo "    build:"
    echo "      $MPV_BUILD"
    echo
    echo "    size before strip:"
    echo "      $SIZE_BEFORE bytes"
    echo
    echo "    size after strip:"
    echo "      $SIZE_AFTER bytes"
    echo
    echo "done."
done

echo
echo "======================================================="
echo ">>> Android libmpv build completed successfully"
echo "======================================================="
echo
echo "Final output root:"
echo "  $FFMPEG_ROOT"
echo
