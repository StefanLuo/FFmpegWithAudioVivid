#!/bin/bash
#
# FFmpeg Android 旗舰版构建脚本
# 适用场景：全能播放内核 (支持 AV3A，LTO 优化)
# 核心特性：支持 AV3A、HTTPS 修复、4K 硬件加速、体积精简
#

set -euo pipefail

# ============================================================
# 1. 环境加载与路径检查
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

# 确保 NPROC 有效
NPROC=$(nproc 2>/dev/null || echo 4)

FFMPEG_DIR="$PROJECT_ROOT/FFmpeg_n9.0.1"
if [ ! -f "$FFMPEG_DIR/configure" ]; then
    echo "[ERROR] FFmpeg 源码目录错误：$FFMPEG_DIR"
    exit 1
fi

cd "$FFMPEG_DIR"

# ============================================================
# 2. 架构选择器
# ============================================================
select_archs() {
    if [ "$#" -eq 1 ]; then
        case "$1" in
            arm64-v8a)   SELECTED_ARCHS=("arm64-v8a") ; return 0 ;;
            armeabi-v7a) SELECTED_ARCHS=("armeabi-v7a") ; return 0 ;;
            x86_64)      SELECTED_ARCHS=("x86_64") ; return 0 ;;
            all)         SELECTED_ARCHS=("arm64-v8a" "armeabi-v7a" "x86_64") ; return 0 ;;
            *) echo "[ERROR] 不支持的 ABI：$1"; exit 1 ;;
        esac
    fi

    echo "======================================================="
    echo "Android FFmpeg ABI 构建选择"
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
            1) SELECTED_ARCHS=("arm64-v8a"); break ;;
            2) SELECTED_ARCHS=("armeabi-v7a"); break ;;
            3) SELECTED_ARCHS=("x86_64"); break ;;
            4) SELECTED_ARCHS=("arm64-v8a" "armeabi-v7a" "x86_64"); break ;;
            0) exit 0 ;;
            *) echo "[ERROR] 无效选择。" ;;
        esac
    done
}

select_archs "$@"

# ============================================================
# 3. 宿主环境净化
# ============================================================
clean_environment() {
    unset CC CXX CPP LD AR AS NM STRIP RANLIB OBJDUMP OBJCOPY READELF
    unset NASM YASM X86ASM
    unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LIBS
    unset PKG_CONFIG PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
}

# ============================================================
# 4. 检查依赖目录
# ============================================================
check_deps() {
    local DEPS_DIR="$1"
    local ARCH="$2"
    echo ">>> 检查 $ARCH 依赖状态..."

    LIBS_TO_CHECK=(
        "libdavs2.a" "libuavs3d.a" "libssl.a" "libcrypto.a" "libdav1d.a"
        "libass.a" "libfreetype.a" "libfribidi.a" "libxml2.a" "libwebp.a"
		"libopus.a" "libogg.a" "libvorbis.a"
    )

    for LIB in "${LIBS_TO_CHECK[@]}"; do
        if [ ! -f "$DEPS_DIR/lib/$LIB" ]; then
            echo "[ERROR] 缺少关键库: $LIB"
            echo "请确保已在 $DEPS_DIR/lib 中准备好交叉编译后的静态库。"
			echo "请运行: build_android_deps.sh $ARCH 生成这些静态库。"
            exit 1
        fi
    done
}

# ============================================================
# 5. 构建主循环
# ============================================================
for ARCH in "${SELECTED_ARCHS[@]}"; do

    echo "======================================================="
    echo ">>> 开始构建 FFmpeg 架构：$ARCH"
    echo "======================================================="

    clean_environment

    # 根据架构配置工具链和参数
    case "$ARCH" in
        arm64-v8a)
            FF_ARCH="aarch64"; FF_CPU="armv8-a"; TARGET="aarch64-linux-android"
            EXTRA_FLAGS="-march=armv8-a+crypto+crc -mcpu=generic"
            ASM_OPTS=("--enable-asm" "--enable-neon")
            ;;
        armeabi-v7a)
            FF_ARCH="arm"; FF_CPU="armv7-a"; TARGET="armv7a-linux-androideabi"
            EXTRA_FLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=softfp"
            ASM_OPTS=("--enable-asm" "--enable-neon")
            ;;
        x86_64)
            FF_ARCH="x86_64"; FF_CPU="x86-64"; TARGET="x86_64-linux-android"
            EXTRA_FLAGS="-march=x86-64"
            ASM_OPTS=("--enable-asm" "--enable-x86asm" "--x86asmexe=nasm")
            ;;
    esac

    DEPS_DIR="$ANDROID_DEPS_ROOT/$ARCH"
    PREFIX="$BUILD_DIR_ROOT/$ARCH"
    LLVM_BIN="$NDK_PATH/toolchains/llvm/prebuilt/windows-x86_64/bin"

    check_deps "$DEPS_DIR" "$ARCH"

    # 设置环境变量指向 NDK 工具链
    export PATH="/mingw64/bin:$LLVM_BIN:/usr/bin:/bin"
    export CC="$LLVM_BIN/${TARGET}${API_LEVEL}-clang"
    export CXX="$LLVM_BIN/${TARGET}${API_LEVEL}-clang++"
    export AR="$LLVM_BIN/llvm-ar"
    export NM="$LLVM_BIN/llvm-nm"
    export RANLIB="$LLVM_BIN/llvm-ranlib"
    export STRIP="$LLVM_BIN/llvm-strip"
    export PKG_CONFIG_LIBDIR="$DEPS_DIR/lib/pkgconfig"
    export PKG_CONFIG_PATH="$DEPS_DIR/lib/pkgconfig"

    # --- 编译旗标优化 ---
    # -O3: 最高等级优化
    # -fomit-frame-pointer: 释放一个通用寄存器，提升解码速度
    # -fPIC: 必须，生成位置无关代码用于共享库
    EXTRA_CFLAGS="-O3 $EXTRA_FLAGS -I$DEPS_DIR/include -fPIC -fomit-frame-pointer"

    # --- 链接参数优化 ---
    # 显式链接 ssl 和 crypto 以支持 HTTPS/TLS
    # -Wl,-Bsymbolic: 减少符号冲突，提升加载速度
    EXTRA_LDFLAGS="-L$DEPS_DIR/lib -lssl -lcrypto -landroid -Wl,-Bsymbolic -Wl,--hash-style=both"

    echo ">>> 执行清理..."
    make distclean >/dev/null 2>&1 || true

    echo ">>> 开始 Configure..."
    $FFMPEG_DIR/configure \
        --prefix="$PREFIX" \
        --target-os=android \
        --arch="$FF_ARCH" \
        --cpu="$FF_CPU" \
        --cross-prefix="$LLVM_BIN/llvm-" \
        --cc="$CC" --cxx="$CXX" \
        --ar="$AR" --nm="$NM" --ranlib="$RANLIB" --strip="$STRIP" \
        --enable-cross-compile \
        --pkg-config=pkg-config \
		--enable-lto \
        --disable-symver \
        --enable-pic \
        --enable-shared --disable-static \
        --enable-gpl --enable-nonfree --enable-version3 \
        --enable-runtime-cpudetect \
        --enable-optimizations \
        --enable-hardcoded-tables \
        --enable-pthreads \
        --enable-network \
        --enable-openssl \
        --enable-jni \
        --enable-mediacodec \
        --enable-libass --enable-libxml2 --enable-libdav1d --enable-libwebp \
		--enable-libopus --enable-libvorbis \
        --enable-libdavs2 --enable-libuavs3d \
        --disable-debug --disable-doc --disable-programs \
        --disable-encoders \
        --disable-muxers \
        --enable-parsers \
        --enable-demuxers \
        --enable-protocols \
        --enable-filters \
        --enable-bsfs \
        --enable-indevs \
        --disable-outdevs \
        --extra-cflags="$EXTRA_CFLAGS" \
        --extra-ldflags="$EXTRA_LDFLAGS" \
        --extra-libs="-lm -lz -ldl" \
        "${ASM_OPTS[@]}"

    echo ">>> 开始并行编译 (线程数: $NPROC)..."
    make -j"$NPROC"
    make install

    echo ">>> FFmpeg $ARCH 构建成功。"
	echo ">>> 产物位于: $PREFIX"
done

echo
echo "======================================================="
echo " FFmpeg Android 构建任务全部完成！"
echo "======================================================="
