#!/bin/bash

set -euo pipefail

# ============================================================
# build_libplacebo_android.sh
#
# Build local Shaderc + local libplacebo for Android.
#
# Output per ABI:
#   android_deps/<ABI>/
#       include/
#           libplacebo/...
#           shaderc/...
#           vulkan/...
#       lib/
#           libshaderc_combined.a
#           libplacebo.a
#           pkgconfig/
#               shaderc.pc
#               vulkan.pc
#               libplacebo.pc
#
# Important:
#   - Uses LOCAL sources only.
#   - No git clone / git pull / network access.
#   - Shaderc is built BEFORE libplacebo so libplacebo can detect it.
#   - libplacebo is built with Vulkan + OpenGL + shaderc.
#   - glslang is NOT used directly by libplacebo; it is built only as
#     an internal Shaderc dependency when required by Shaderc.
#   - No Meson --prefix is passed.
#   - libplacebo installation is staged with DESTDIR and then copied.
#   - Designed for MSYS2 MINGW64 + Android NDK on Windows.
#
# Expected local sources:
#   $PROJECT_ROOT/libplacebo/
#   $PROJECT_ROOT/shaderc/
#
# Shaderc source tree must already contain its local dependencies:
#   shaderc/third_party/glslang/
#   shaderc/third_party/spirv-headers/
#   shaderc/third_party/spirv-tools/
#
# ============================================================


# ============================================================
# 1. Script / project paths
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


# ============================================================
# 2. Required environment
# ============================================================

: "${PROJECT_ROOT:?PROJECT_ROOT is not set}"
: "${NDK_PATH:?NDK_PATH is not set}"
: "${API_LEVEL:?API_LEVEL is not set}"
: "${ANDROID_DEPS_ROOT:?ANDROID_DEPS_ROOT is not set}"
: "${OPT_FLAGS:?OPT_FLAGS is not set}"


# ============================================================
# 3. Local source trees
# ============================================================

LIBPLACEBO_ROOT="$PROJECT_ROOT/libplacebo"
SHADERC_ROOT="${SHADERC_ROOT:-$PROJECT_ROOT/shaderc}"
SHADERC_PKG_VERSION="${SHADERC_PKG_VERSION:-2023.8}"

if [ ! -f "$LIBPLACEBO_ROOT/meson.build" ]; then
    echo
    echo "[ERROR] Local libplacebo source not found or incomplete:"
    echo "        $LIBPLACEBO_ROOT/meson.build"
    exit 1
fi

if [ ! -f "$LIBPLACEBO_ROOT/meson_options.txt" ]; then
    echo
    echo "[ERROR] libplacebo meson_options.txt not found:"
    echo "        $LIBPLACEBO_ROOT/meson_options.txt"
    exit 1
fi

if [ ! -f "$SHADERC_ROOT/CMakeLists.txt" ]; then
    echo
    echo "[ERROR] Local Shaderc source not found or incomplete:"
    echo "        $SHADERC_ROOT/CMakeLists.txt"
    echo
    echo "Expected local source directory:"
    echo "        $PROJECT_ROOT/shaderc"
    exit 1
fi

# ------------------------------------------------------------
# libplacebo bundled third-party source trees
# ------------------------------------------------------------

JINJA_DIR="$LIBPLACEBO_ROOT/3rdparty/jinja"
MARKUPSAFE_DIR="$LIBPLACEBO_ROOT/3rdparty/markupsafe"
GLAD_DIR="$LIBPLACEBO_ROOT/3rdparty/glad"
VULKAN_HEADERS_DIR="$LIBPLACEBO_ROOT/3rdparty/Vulkan-Headers"

for DIR in \
    "$JINJA_DIR" \
    "$MARKUPSAFE_DIR" \
    "$GLAD_DIR" \
    "$VULKAN_HEADERS_DIR"
do
    if [ ! -d "$DIR" ]; then
        echo
        echo "[ERROR] Required local libplacebo third-party source tree missing:"
        echo "        $DIR"
        echo
        echo "This script will NOT fetch it from Git."
        exit 1
    fi
done

# ------------------------------------------------------------
# Shaderc local third-party source trees
# ------------------------------------------------------------

SHADERC_GLSLANG_DIR="$SHADERC_ROOT/third_party/glslang"
SHADERC_SPIRV_HEADERS_DIR="$SHADERC_ROOT/third_party/spirv-headers"
SHADERC_SPIRV_TOOLS_DIR="$SHADERC_ROOT/third_party/spirv-tools"

for DIR in \
    "$SHADERC_GLSLANG_DIR" \
    "$SHADERC_SPIRV_HEADERS_DIR" \
    "$SHADERC_SPIRV_TOOLS_DIR"
do
    if [ ! -d "$DIR" ]; then
        echo
        echo "[ERROR] Shaderc local dependency tree missing:"
        echo "        $DIR"
        echo
        echo "The Shaderc source tree is incomplete."
        echo "No network download is performed by this script."
        exit 1
    fi
done

if [ ! -f "$SHADERC_ROOT/libshaderc/CMakeLists.txt" ]; then
    echo "[ERROR] Shaderc libshaderc CMakeLists.txt not found:"
    echo "        $SHADERC_ROOT/libshaderc/CMakeLists.txt"
    exit 1
fi

if [ ! -d "$SHADERC_ROOT/libshaderc/include/shaderc" ]; then
    echo "[ERROR] Shaderc public headers not found:"
    echo "        $SHADERC_ROOT/libshaderc/include/shaderc"
    exit 1
fi


# ============================================================
# 4. Android NDK toolchain
# ============================================================

HOST_TAG="windows-x86_64"
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG"
LLVM_BIN="$TOOLCHAIN/bin"
SYSROOT="$TOOLCHAIN/sysroot"
ANDROID_CMAKE_TOOLCHAIN="$NDK_PATH/build/cmake/android.toolchain.cmake"

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
# 5. Build tools
# ============================================================

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
    "$LLVM_BIN/llvm-strip.exe"
do
    if [ ! -f "$TOOL" ]; then
        echo "[ERROR] Required tool not found:"
        echo "        $TOOL"
        exit 1
    fi
done

if [ -z "$CMAKE_BIN" ] || [ ! -f "$CMAKE_BIN" ]; then
    echo "[ERROR] CMake not found in PATH."
    exit 1
fi

if [ -z "$PKG_CONFIG_EXE" ]; then
    echo "[ERROR] pkg-config not found in PATH."
    exit 1
fi

if [ -z "$PYTHON_BIN" ]; then
    echo "[ERROR] Python 3 not found in PATH."
    exit 1
fi


# ============================================================
# 6. Python / Jinja2
# ============================================================

# Make the bundled Jinja2 and MarkupSafe importable without requiring
# a system pip installation.
export PYTHONPATH="$JINJA_DIR/src:$MARKUPSAFE_DIR/src${PYTHONPATH:+:$PYTHONPATH}"

if ! "$PYTHON_BIN" -c 'import jinja2, markupsafe' >/dev/null 2>&1; then
    echo
    echo "[ERROR] Bundled Jinja2 / MarkupSafe could not be imported."
    echo "        Python      : $PYTHON_BIN"
    echo "        Jinja path  : $JINJA_DIR/src"
    echo "        Markup path : $MARKUPSAFE_DIR/src"
    exit 1
fi


# ============================================================
# 7. ABI selector
# ============================================================

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
    echo "MPV Dependencies Android ABI Selector"
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

NPROC="$(nproc 2>/dev/null || echo 4)"


# ============================================================
# 8. Android API requirements
# ============================================================

if [ "$API_LEVEL" -lt 26 ]; then
    echo
    echo "[ERROR] API_LEVEL=$API_LEVEL is too low."
    echo "        This project targets minSdk 26 and requires Vulkan/AAudio."
    echo "        Set API_LEVEL=26 in build_scripts/env.sh."
    exit 1
fi


# ============================================================
# 9. Global summary
# ============================================================

echo
echo "======================================================="
echo ">>> libplacebo + Shaderc Android build environment"
echo "======================================================="
echo "    PROJECT_ROOT        = $PROJECT_ROOT"
echo "    LIBPLACEBO_ROOT     = $LIBPLACEBO_ROOT"
echo "    SHADERC_ROOT        = $SHADERC_ROOT"
echo "    NDK_PATH            = $NDK_PATH"
echo "    API_LEVEL           = $API_LEVEL"
echo "    ANDROID_DEPS_ROOT   = $ANDROID_DEPS_ROOT"
echo "    MESON               = $MESON_BIN"
echo "    CMAKE               = $CMAKE_BIN"
echo "    NINJA               = $NINJA_BIN"
echo "    PYTHON              = $PYTHON_BIN"
echo "    NPROC               = $NPROC"
echo "    Vulkan              = enabled"
echo "    OpenGL              = enabled"
echo "    vk-proc-addr        = enabled"
echo "    gl-proc-addr        = enabled"
echo "    Shaderc             = enabled"
echo "    glslang(libplacebo) = disabled"
echo "    D3D11               = disabled"
echo "    LCMS                = disabled"
echo "    libdovi             = disabled"
echo "    unwind              = disabled"
echo "    xxhash              = disabled"
echo "======================================================="
echo


# ============================================================
# 10. Build each ABI
# ============================================================

for ARCH in "${SELECTED_ARCHS[@]}"; do

    echo
    echo
    echo "======================================================="
    echo ">>> Building Shaderc + libplacebo [$ARCH]"
    echo "======================================================="

    # --------------------------------------------------------
    # ABI configuration
    # --------------------------------------------------------

    case "$ARCH" in
        arm64-v8a)
            CLANG_TRIPLE="aarch64-linux-android${API_LEVEL}"
            MESON_CPU_FAMILY="aarch64"
            MESON_CPU="arm64"
            ;;
        armeabi-v7a)
            CLANG_TRIPLE="armv7a-linux-androideabi${API_LEVEL}"
            MESON_CPU_FAMILY="arm"
            MESON_CPU="armv7-a"
            ;;
        x86_64)
            CLANG_TRIPLE="x86_64-linux-android${API_LEVEL}"
            MESON_CPU_FAMILY="x86_64"
            MESON_CPU="x86_64"
            ;;
    esac

    # --------------------------------------------------------
    # Paths
    # --------------------------------------------------------

    DEPS_PREFIX="$ANDROID_DEPS_ROOT/$ARCH"
    BUILD_ROOT="$PROJECT_ROOT/android_build_deps/$ARCH"
    SHADERC_BUILD="$BUILD_ROOT/shaderc"
    LIBPLACEBO_BUILD="$BUILD_ROOT/libplacebo"
    LIBPLACEBO_STAGE="$LIBPLACEBO_BUILD/stage"
    CROSS_FILE="$LIBPLACEBO_BUILD/cross_file.txt"

    rm -rf -- "$SHADERC_BUILD" "$LIBPLACEBO_BUILD"

    mkdir -p -- \
        "$DEPS_PREFIX/include" \
        "$DEPS_PREFIX/lib" \
        "$DEPS_PREFIX/lib/pkgconfig" \
        "$SHADERC_BUILD" \
        "$LIBPLACEBO_BUILD"

    # --------------------------------------------------------
    # NDK tools
    # --------------------------------------------------------

    CLANG_EXE="$LLVM_BIN/clang.exe"
    CLANGXX_EXE="$LLVM_BIN/clang++.exe"
    AR="$LLVM_BIN/llvm-ar.exe"
    NM="$LLVM_BIN/llvm-nm.exe"
    RANLIB="$LLVM_BIN/llvm-ranlib.exe"
    STRIP="$LLVM_BIN/llvm-strip.exe"

    CLANG_EXE_WIN="$(cygpath -m "$CLANG_EXE")"
    CLANGXX_EXE_WIN="$(cygpath -m "$CLANGXX_EXE")"
    AR_WIN="$(cygpath -m "$AR")"
    NM_WIN="$(cygpath -m "$NM")"
    RANLIB_WIN="$(cygpath -m "$RANLIB")"
    STRIP_WIN="$(cygpath -m "$STRIP")"

    ANDROID_CMAKE_TOOLCHAIN_WIN="$(cygpath -m "$ANDROID_CMAKE_TOOLCHAIN")"
    SHADERC_ROOT_WIN="$(cygpath -m "$SHADERC_ROOT")"
    SHADERC_BUILD_WIN="$(cygpath -m "$SHADERC_BUILD")"
    LIBPLACEBO_ROOT_WIN="$(cygpath -m "$LIBPLACEBO_ROOT")"
    LIBPLACEBO_BUILD_WIN="$(cygpath -m "$LIBPLACEBO_BUILD")"
    CROSS_FILE_WIN="$(cygpath -m "$CROSS_FILE")"
    DEPS_PREFIX_WIN="$(cygpath -m "$DEPS_PREFIX")"
    VULKAN_HEADERS_WIN="$(cygpath -m "$VULKAN_HEADERS_DIR/include")"
    VK_XML_WIN="$(cygpath -m "$VULKAN_HEADERS_DIR/registry/vk.xml")"

    PKG_CONFIG_WIN="$(cygpath -m "$PKG_CONFIG_EXE")"
    PYTHON_WIN="$(cygpath -m "$PYTHON_BIN")"

    # --------------------------------------------------------
    # Vulkan headers / registry validation
    # --------------------------------------------------------

    if [ ! -f "$VULKAN_HEADERS_DIR/include/vulkan/vulkan.h" ]; then
        echo "[ERROR] Vulkan headers missing:"
        echo "        $VULKAN_HEADERS_DIR/include/vulkan/vulkan.h"
        exit 1
    fi

    if [ ! -f "$VULKAN_HEADERS_DIR/registry/vk.xml" ]; then
        echo "[ERROR] Vulkan registry missing:"
        echo "        $VULKAN_HEADERS_DIR/registry/vk.xml"
        exit 1
    fi

    # Android NDK supplies the Vulkan loader for API 24+, but the actual
    # link test is deferred to the Android target compiler. We only verify
    # that the sysroot contains the expected loader file for this ABI.
    ANDROID_VULKAN_LIB="$($CLANG_EXE \
        --target="$CLANG_TRIPLE" \
        --sysroot="$SYSROOT" \
        -print-file-name=libvulkan.so)"

    if [ -z "$ANDROID_VULKAN_LIB" ] || [ ! -f "$ANDROID_VULKAN_LIB" ]; then
        echo "[ERROR] Android NDK libvulkan.so not found."
        echo "        ABI    : $ARCH"
        echo "        target : $CLANG_TRIPLE"
        echo "        result : $ANDROID_VULKAN_LIB"
        exit 1
    fi

    # --------------------------------------------------------
    # Publish Vulkan headers into the ABI dependency prefix.
    # This keeps the pkg-config tree self-contained and avoids any
    # dependency on a host Vulkan SDK when mpv is configured later.
    # --------------------------------------------------------

    rm -rf -- "$DEPS_PREFIX/include/vulkan"
    cp -a \
        "$VULKAN_HEADERS_DIR/include/vulkan" \
        "$DEPS_PREFIX/include/"

    # --------------------------------------------------------
    # pkg-config isolation for the ABI
    # --------------------------------------------------------

    export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig"
    export PKG_CONFIG_PATH=""
    export PKG_CONFIG_SYSTEM_LIBRARY_PATH=""
    export PKG_CONFIG_SYSTEM_INCLUDE_PATH=""
    unset PKG_CONFIG_SYSROOT_DIR 2>/dev/null || true

    echo
    echo ">>> ABI paths"
    echo "    DEPS_PREFIX       = $DEPS_PREFIX"
    echo "    BUILD_ROOT        = $BUILD_ROOT"
    echo "    SHADERC_BUILD     = $SHADERC_BUILD"
    echo "    LIBPLACEBO_BUILD  = $LIBPLACEBO_BUILD"
    echo "    TARGET            = $CLANG_TRIPLE"


    # ========================================================
    # 11. Shaderc build
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building Shaderc [$ARCH]"
    echo "======================================================="
	echo
	echo ">>> CMake paths"
	echo "    Python for CMake = $PYTHON_WIN"
	echo
    # Shaderc is a CMake project. Use the same Android NDK toolchain
    # and libc++ runtime that the final libmpv stack will use.
    "$CMAKE_BIN" \
        -S "$SHADERC_ROOT_WIN" \
        -B "$SHADERC_BUILD_WIN" \
        -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_CMAKE_TOOLCHAIN_WIN" \
        -DANDROID_ABI="$ARCH" \
        -DANDROID_PLATFORM="android-$API_LEVEL" \
        -DANDROID_STL=c++_shared \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_STANDARD=11 \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_CXX_STANDARD_REQUIRED=ON \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_C_FLAGS="$OPT_FLAGS -fPIC" \
        -DCMAKE_CXX_FLAGS="$OPT_FLAGS -fPIC" \
		-DPython_EXECUTABLE="$PYTHON_WIN" \
		-DPython3_EXECUTABLE="$PYTHON_WIN" \
		-DPYTHON_EXECUTABLE="$PYTHON_WIN" \
		-DPython3_FIND_REGISTRY=NEVER \
		-DPython3_FIND_STRATEGY=LOCATION \
        -DSHADERC_SKIP_TESTS=ON \
        -DSHADERC_SKIP_EXAMPLES=ON \
        -DSHADERC_SKIP_COPYRIGHT_CHECK=ON \
        -DSHADERC_ENABLE_HLSL=OFF \
        -DSHADERC_ENABLE_WGSL_OUTPUT=OFF

    "$NINJA_BIN" \
        -C "$SHADERC_BUILD_WIN" \
        -j"$NPROC" \
        shaderc_combined

    SHADERC_LIB=""

    if [ -f "$SHADERC_BUILD/libshaderc/libshaderc_combined.a" ]; then
        SHADERC_LIB="$SHADERC_BUILD/libshaderc/libshaderc_combined.a"
    else
        SHADERC_LIB="$(find "$SHADERC_BUILD" -type f -name 'libshaderc_combined.a' -print -quit)"
    fi

    if [ -z "$SHADERC_LIB" ] || [ ! -f "$SHADERC_LIB" ]; then
        echo
        echo "[ERROR] libshaderc_combined.a not found."
        echo "        Build directory: $SHADERC_BUILD"
        exit 1
    fi

    cp -f \
        "$SHADERC_LIB" \
        "$DEPS_PREFIX/lib/libshaderc_combined.a"

    # --------------------------------------------------------
    # Shaderc public headers
    # --------------------------------------------------------

    rm -rf -- "$DEPS_PREFIX/include/shaderc"
    mkdir -p -- "$DEPS_PREFIX/include/shaderc"

    cp -a \
        "$SHADERC_ROOT/libshaderc/include/shaderc/." \
        "$DEPS_PREFIX/include/shaderc/"

    # --------------------------------------------------------
    # shaderc.pc
    #
    # The important part is that the module is named exactly "shaderc".
    # libplacebo looks for this pkg-config module.
    #
    # libshaderc_combined already contains the Shaderc/glslang/SPIR-V
    # static objects, so consumers should not separately link those.
    # libc++_shared is exposed through Libs because libplacebo is itself
    # a static library and the final link is performed by mpv.
    # --------------------------------------------------------

    cat > "$DEPS_PREFIX/lib/pkgconfig/shaderc.pc" <<EOF_SHADERC
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: shaderc
Description: Shader compilation library for Vulkan
Version: ${SHADERC_PKG_VERSION}
Cflags: -I\${includedir}
Libs: -L\${libdir} -lshaderc_combined -lc++_shared -lm -ldl -pthread
EOF_SHADERC

    if ! pkg-config --exists shaderc; then
        echo "[ERROR] shaderc pkg-config module is not visible."
        exit 1
    fi

    echo
    echo ">>> Shaderc verification"
    echo "    Library:"
    echo "      $DEPS_PREFIX/lib/libshaderc_combined.a"
    echo "    Cflags:"
    pkg-config --cflags shaderc
    echo "    Libs:"
    pkg-config --libs shaderc


    # ========================================================
    # 12. Vulkan pkg-config shim
    # ========================================================

    cat > "$DEPS_PREFIX/lib/pkgconfig/vulkan.pc" <<EOF_VULKAN
prefix=$DEPS_PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: Vulkan
Description: Android NDK Vulkan loader and headers
Version: 1.3-local
Cflags: -I\${includedir}
Libs: -lvulkan
EOF_VULKAN

    if ! pkg-config --exists vulkan; then
        echo "[ERROR] Vulkan pkg-config module is not visible."
        exit 1
    fi

    echo
    echo ">>> Vulkan verification"
    echo "    NDK loader:"
    echo "      $ANDROID_VULKAN_LIB"
    echo "    Headers:"
    echo "      $DEPS_PREFIX/include/vulkan"
    echo "    Registry:"
    echo "      $VULKAN_HEADERS_DIR/registry/vk.xml"
    echo "    Cflags:"
    pkg-config --cflags vulkan
    echo "    Libs:"
    pkg-config --libs vulkan


    # ========================================================
    # 13. libplacebo Meson cross file
    # ========================================================

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
pkg_config_libdir = ['$DEPS_PREFIX_WIN/lib/pkgconfig']
EOF_CROSS

    echo
    echo ">>> libplacebo cross file"
    sed -n '1,120p' "$CROSS_FILE"


    # ========================================================
    # 14. libplacebo Meson setup
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Configuring libplacebo [$ARCH]"
    echo "======================================================="

    # IMPORTANT:
    #   Do not pass --prefix here.
    #   Meson uses its normal /usr/local prefix internally and the
    #   install is captured with DESTDIR below.
    "$MESON_BIN" setup \
        "$(cygpath -m "$LIBPLACEBO_BUILD")" \
        "$LIBPLACEBO_ROOT_WIN" \
        --cross-file "$CROSS_FILE_WIN" \
        --libdir lib \
        --default-library static \
        --buildtype release \
        -Ddemos=false \
        -Dtests=false \
        -Dbench=false \
        -Dfuzz=false \
        -Dvulkan=enabled \
        -Dvk-proc-addr=enabled \
        -Dvulkan-registry="$VK_XML_WIN" \
        -Dopengl=enabled \
        -Dgl-proc-addr=enabled \
        -Dd3d11=disabled \
        -Dshaderc=enabled \
        -Dglslang=disabled \
        -Dlcms=disabled \
        -Ddovi=disabled \
        -Dlibdovi=disabled \
        -Dunwind=disabled \
        -Dxxhash=disabled

    echo
    echo "======================================================="
    echo ">>> libplacebo configuration [$ARCH]"
    echo "======================================================="

    "$MESON_BIN" configure "$(cygpath -m "$LIBPLACEBO_BUILD")" \
        | grep -E \
            "vulkan|vk-proc-addr|opengl|gl-proc-addr|shaderc|glslang|d3d11|lcms|dovi|libdovi|unwind|xxhash|tests|bench|demos|default_library" \
        || true


    # ========================================================
    # 15. Build libplacebo
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Building libplacebo [$ARCH]"
    echo "======================================================="

    "$NINJA_BIN" \
        -C "$(cygpath -m "$LIBPLACEBO_BUILD")" \
        -j"$NPROC"


    # ========================================================
    # 16. Staged install
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Staging libplacebo install [$ARCH]"
    echo "======================================================="

    rm -rf -- "$LIBPLACEBO_STAGE"
    mkdir -p -- "$LIBPLACEBO_STAGE"

    DESTDIR="$(cygpath -m "$LIBPLACEBO_STAGE")" \
        "$NINJA_BIN" \
        -C "$(cygpath -m "$LIBPLACEBO_BUILD")" \
        install


    # ========================================================
    # 17. Locate staged files
    # ========================================================

    LIBPLACEBO_LIB="$(find "$LIBPLACEBO_STAGE" \
        -type f \
        -name 'libplacebo.a' \
        -print -quit)"

    LIBPLACEBO_PC="$(find "$LIBPLACEBO_STAGE" \
        -type f \
        -name 'libplacebo.pc' \
        -print -quit)"

    STAGE_INCLUDE="$(find "$LIBPLACEBO_STAGE" \
        -type d \
        -path '*/include/libplacebo' \
        -print -quit)"

    if [ -z "$LIBPLACEBO_LIB" ] || [ ! -f "$LIBPLACEBO_LIB" ]; then
        echo
        echo "[ERROR] Staged libplacebo.a not found."
        echo "        Stage: $LIBPLACEBO_STAGE"
        exit 1
    fi

    if [ -z "$LIBPLACEBO_PC" ] || [ ! -f "$LIBPLACEBO_PC" ]; then
        echo
        echo "[ERROR] Staged libplacebo.pc not found."
        echo "        Stage: $LIBPLACEBO_STAGE"
        exit 1
    fi

    if [ -z "$STAGE_INCLUDE" ] || [ ! -d "$STAGE_INCLUDE" ]; then
        echo
        echo "[ERROR] Staged libplacebo headers not found."
        echo "        Stage: $LIBPLACEBO_STAGE"
        exit 1
    fi


    # ========================================================
    # 18. Install final libplacebo package into android_deps
    # ========================================================

    rm -rf -- "$DEPS_PREFIX/include/libplacebo"
    mkdir -p -- "$DEPS_PREFIX/include/libplacebo"

    cp -a \
        "$STAGE_INCLUDE/." \
        "$DEPS_PREFIX/include/libplacebo/"

    cp -f \
        "$LIBPLACEBO_LIB" \
        "$DEPS_PREFIX/lib/libplacebo.a"

    cp -f \
        "$LIBPLACEBO_PC" \
        "$DEPS_PREFIX/lib/pkgconfig/libplacebo.pc"

    # Rewrite only installation prefixes. Keep all generated Requires /
    # private link metadata intact.
    sed -i \
        "s|^prefix=.*$|prefix=$DEPS_PREFIX|" \
        "$DEPS_PREFIX/lib/pkgconfig/libplacebo.pc"

    sed -i \
        's|^exec_prefix=.*$|exec_prefix=${prefix}|' \
        "$DEPS_PREFIX/lib/pkgconfig/libplacebo.pc"

    sed -i \
        's|^libdir=.*$|libdir=${prefix}/lib|' \
        "$DEPS_PREFIX/lib/pkgconfig/libplacebo.pc"

    sed -i \
        's|^includedir=.*$|includedir=${prefix}/include|' \
        "$DEPS_PREFIX/lib/pkgconfig/libplacebo.pc"


    # ========================================================
    # 19. Final verification
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> Final package verification [$ARCH]"
    echo "======================================================="

    if [ ! -f "$DEPS_PREFIX/lib/libshaderc_combined.a" ]; then
        echo "[ERROR] Missing final Shaderc library."
        exit 1
    fi

    if [ ! -f "$DEPS_PREFIX/lib/libplacebo.a" ]; then
        echo "[ERROR] Missing final libplacebo library."
        exit 1
    fi

    if [ ! -f "$DEPS_PREFIX/lib/pkgconfig/shaderc.pc" ]; then
        echo "[ERROR] Missing shaderc.pc."
        exit 1
    fi

    if [ ! -f "$DEPS_PREFIX/lib/pkgconfig/vulkan.pc" ]; then
        echo "[ERROR] Missing vulkan.pc."
        exit 1
    fi

    if [ ! -f "$DEPS_PREFIX/lib/pkgconfig/libplacebo.pc" ]; then
        echo "[ERROR] Missing libplacebo.pc."
        exit 1
    fi

    if ! pkg-config --exists shaderc; then
        echo "[ERROR] shaderc pkg-config verification failed."
        exit 1
    fi

    if ! pkg-config --exists vulkan; then
        echo "[ERROR] vulkan pkg-config verification failed."
        exit 1
    fi

    if ! pkg-config --exists libplacebo; then
        echo "[ERROR] libplacebo pkg-config verification failed."
        exit 1
    fi

    echo
    echo "[shaderc]"
    pkg-config --modversion shaderc
    pkg-config --cflags shaderc
    pkg-config --libs shaderc

    echo
    echo "[vulkan]"
    pkg-config --modversion vulkan
    pkg-config --cflags vulkan
    pkg-config --libs vulkan

    echo
    echo "[libplacebo]"
    pkg-config --modversion libplacebo
    pkg-config --cflags libplacebo
    pkg-config --libs libplacebo

    echo
    echo "[libplacebo static]"
    pkg-config --libs --static libplacebo || true

    echo
    echo ">>> libplacebo archive sample"
    "$LLVM_BIN/llvm-ar.exe" t \
        "$DEPS_PREFIX/lib/libplacebo.a" \
        | head -20

    echo
    echo ">>> Shaderc archive sample"
    "$LLVM_BIN/llvm-ar.exe" t \
        "$DEPS_PREFIX/lib/libshaderc_combined.a" \
        | head -20

    echo
    echo "======================================================="
    echo ">>> [$ARCH] completed successfully"
    echo "======================================================="
    echo
    echo "    Shaderc:"
    echo "      $DEPS_PREFIX/lib/libshaderc_combined.a"
    echo
    echo "    libplacebo:"
    echo "      $DEPS_PREFIX/lib/libplacebo.a"
    echo
    echo "    libplacebo headers:"
    echo "      $DEPS_PREFIX/include/libplacebo"
    echo
    echo "    Shaderc headers:"
    echo "      $DEPS_PREFIX/include/shaderc"
    echo
    echo "    Vulkan headers:"
    echo "      $DEPS_PREFIX/include/vulkan"
    echo
    echo "    pkg-config:"
    echo "      $DEPS_PREFIX/lib/pkgconfig"
    echo

done


# ============================================================
# 20. Completed
# ============================================================

echo
echo "======================================================="
echo ">>> libplacebo + Shaderc Android build completed"
echo "======================================================="
echo
echo "Final output root:"
echo "  $ANDROID_DEPS_ROOT"
echo
echo "Note: Shaderc uses libc++_shared. Your final Android App should"
echo "package the matching libc++_shared.so for each ABI." 
echo
