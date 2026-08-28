#!/bin/bash
#
# FFmpeg Android 三架构构建脚本
# Windows + MSYS2 + Android NDK
#
# 支持：
#   1. arm64-v8a
#   2. armeabi-v7a
#   3. x86_64
#   4. 全部架构
#
# 用法：
#
#   ./build_android.sh
#       进入交互式 ABI 选择器
#
#   ./build_android.sh arm64-v8a
#       直接构建 arm64-v8a
#
#   ./build_android.sh armeabi-v7a
#       直接构建 armeabi-v7a
#
#   ./build_android.sh x86_64
#       直接构建 x86_64
#
# 核心目标：
#   1. 完全隔离 MSYS2 宿主机 x86_64 工具链
#   2. ARM 架构禁止使用 X86ASM
#   3. 每个架构使用独立 build/config 状态
#   4. 每个架构使用独立 android_deps
#   5. 强制使用 Android NDK LLVM 工具链
#   6. 防止 libavcodec/x86/*.o 被加入 ARM 构建
#   7. 单独构建某个 ABI 时不影响其他 ABI
#

set -e

# ============================================================
# 获取 build_android.sh 所在目录
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ============================================================
# 加载环境
# ============================================================

source "$SCRIPT_DIR/env.sh"

# ============================================================
# FFmpeg 源码目录
# ============================================================

FFMPEG_DIR="$PROJECT_ROOT/FFmpeg_n9.0.1"

if [ ! -f "$FFMPEG_DIR/configure" ]; then
    echo "[ERROR] FFmpeg 源码目录错误：$FFMPEG_DIR"
    exit 1
fi

# ============================================================
# 基础目录
# ============================================================

BUILD_DIR_ROOT="$BUILD_DIR_ROOT"
ANDROID_DEPS_ROOT="$ANDROID_DEPS_ROOT"

# ============================================================
# 进入 FFmpeg 源码目录
# ============================================================

cd "$FFMPEG_DIR"

echo
echo "======================================================="
echo "FFmpeg Android Multi-ABI Build"
echo "======================================================="
echo "PROJECT_ROOT      : $PROJECT_ROOT"
echo "FFMPEG_DIR        : $FFMPEG_DIR"
echo "BUILD_DIR_ROOT    : $BUILD_DIR_ROOT"
echo "ANDROID_DEPS_ROOT : $ANDROID_DEPS_ROOT"
echo "SCRIPT_DIR        : $SCRIPT_DIR"
echo "NDK_PATH          : $NDK_PATH"
echo "API_LEVEL         : $API_LEVEL"
echo "======================================================="
echo

# ============================================================
# 1. 检查 NDK
# ============================================================

if [ -z "$NDK_PATH" ]; then
    echo "[ERROR] NDK_PATH 未设置"
    exit 1
fi

HOST_TAG="windows-x86_64"

TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG"
LLVM_BIN="$TOOLCHAIN/bin"

if [ ! -d "$TOOLCHAIN" ]; then
    echo "[ERROR] 找不到 Android NDK LLVM 工具链："
    echo "$TOOLCHAIN"
    exit 1
fi

if [ ! -d "$LLVM_BIN" ]; then
    echo "[ERROR] LLVM bin 不存在："
    echo "$LLVM_BIN"
    exit 1
fi

# ============================================================
# 2. FFmpeg 源码完整性检查
#
# common.mak 是 FFmpeg 源码文件，不是 configure 生成文件。
# 不存在时直接停止，避免进入 make 才发现源码不完整。
# ============================================================

if [ ! -f "$FFMPEG_DIR/ffbuild/common.mak" ]; then
    echo
    echo "[ERROR] FFmpeg 源码缺少："
    echo "        $FFMPEG_DIR/ffbuild/common.mak"
    echo
    echo "请先恢复完整的 FFmpeg n9.0.1 源码。"
    exit 1
fi

# ============================================================
# 3. 架构选择器
# ============================================================

select_archs()
{
    # 命令行直接指定 ABI
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
                echo "[ERROR] 不支持的 ABI：$1"
                echo
                echo "支持："
                echo "  arm64-v8a"
                echo "  armeabi-v7a"
                echo "  x86_64"
                echo "  all"
                exit 1
                ;;

        esac

    elif [ "$#" -gt 1 ]; then

        echo "[ERROR] 参数过多"
        echo
        echo "用法："
        echo "  $0"
        echo "  $0 arm64-v8a"
        echo "  $0 armeabi-v7a"
        echo "  $0 x86_64"
        echo "  $0 all"
        exit 1

    fi

    # --------------------------------------------------------
    # 无参数 → 进入选择器
    # --------------------------------------------------------

    echo
    echo "======================================================="
    echo "Android FFmpeg ABI 构建选择"
    echo "======================================================="
    echo
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
                SELECTED_ARCHS=(
                    "arm64-v8a"
                    "armeabi-v7a"
                    "x86_64"
                )
                break
                ;;

            0)
                echo
                echo "已退出。"
                exit 0
                ;;

            *)
                echo "[ERROR] 无效选择，请输入 0-4。"
                ;;

        esac

    done
}

# ============================================================
# 4. 调用架构选择器
# ============================================================

select_archs "$@"

# ============================================================
# 5. MSYS2 宿主环境净化
#
# 这里不修改 PATH。
# 每个 ABI 在后面重新设置 PATH。
# ============================================================

clean_environment()
{
    echo ">>> 清理宿主编译环境..."

    unset CC
    unset CXX
    unset CPP
    unset LD
    unset AR
    unset AS
    unset NM
    unset STRIP
    unset RANLIB
    unset OBJDUMP
    unset OBJCOPY
    unset READELF

    unset NASM
    unset YASM
    unset X86ASM

    unset CFLAGS
    unset CXXFLAGS
    unset CPPFLAGS
    unset LDFLAGS
    unset LIBS

    unset PKG_CONFIG
    unset PKG_CONFIG_PATH
    unset PKG_CONFIG_LIBDIR
    unset PKG_CONFIG_SYSROOT_DIR
}

# ============================================================
# 6. 显示工具链
# ============================================================

show_toolchain()
{
    echo
    echo ">>> 当前工具链："

    echo "CC      = $CC"
    echo "CXX     = $CXX"
    echo "AR      = $AR"
    echo "NM      = $NM"
    echo "RANLIB  = $RANLIB"
    echo "STRIP   = $STRIP"

    echo
    echo ">>> 工具实际位置："

    command -v "$CC" || true
    command -v "$CXX" || true
    command -v "$AR" || true
    command -v "$NM" || true
    command -v "$RANLIB" || true
    command -v "$STRIP" || true

    echo
}

# ============================================================
# 7. 检查当前 ABI 依赖
# ============================================================

check_deps()
{
    local DEPS_DIR="$1"

    echo ">>> 检查依赖目录：$DEPS_DIR"

    if [ ! -d "$DEPS_DIR/include" ]; then
        echo "[ERROR] include 不存在："
        echo "        $DEPS_DIR/include"
        exit 1
    fi

    if [ ! -d "$DEPS_DIR/lib" ]; then
        echo "[ERROR] lib 不存在："
        echo "        $DEPS_DIR/lib"
        exit 1
    fi

    if [ ! -f "$DEPS_DIR/lib/libdavs2.a" ]; then
        echo "[ERROR] libdavs2.a 不存在："
        echo "        $DEPS_DIR/lib/libdavs2.a"
        echo
        echo "请先执行："
        echo "  ./build_deps_android.sh $ARCH"
        exit 1
    fi

    if [ ! -f "$DEPS_DIR/lib/libuavs3d.a" ]; then
        echo "[ERROR] libuavs3d.a 不存在："
        echo "        $DEPS_DIR/lib/libuavs3d.a"
        echo
        echo "请先执行："
        echo "  ./build_deps_android.sh $ARCH"
        exit 1
    fi

    if [ ! -f "$DEPS_DIR/lib/pkgconfig/davs2.pc" ]; then
        echo "[ERROR] davs2.pc 不存在"
        exit 1
    fi

    if [ ! -f "$DEPS_DIR/lib/pkgconfig/uavs3d.pc" ]; then
        echo "[ERROR] uavs3d.pc 不存在"
        exit 1
    fi

    echo ">>> 依赖检查通过"
    echo
}

# ============================================================
# 8. 检查 configure 最终架构
# ============================================================

verify_config()
{
    local ARCH="$1"

    echo
    echo ">>> 检查 $ARCH configure 架构..."

    if [ ! -f config.h ]; then
        echo "[ERROR] config.h 不存在"
        exit 1
    fi

    if [ ! -f ffbuild/config.mak ]; then
        echo "[ERROR] ffbuild/config.mak 不存在"
        exit 1
    fi

    echo
    echo "----- ARCH 相关配置 -----"

    grep -E \
        'CONFIG_X86|CONFIG_X86_64|HAVE_X86|ARCH_X86|ARCH_AARCH64|ARCH_ARM' \
        config.h \
        2>/dev/null || true

    echo
    echo "----- config.mak 中的架构信息 -----"

    grep -Ei \
        'x86|aarch64|arm|x86_64' \
        ffbuild/config.mak \
        2>/dev/null | head -80 || true

    echo

    # --------------------------------------------------------
    # arm64
    # --------------------------------------------------------

    if [ "$ARCH" = "arm64-v8a" ]; then

        if grep -Eq \
            'CONFIG_X86(_64)?[[:space:]]+1' \
            config.h \
            2>/dev/null
        then
            echo "[ERROR] ARM64 configure 被错误识别为 X86/X86_64"
            exit 1
        fi

        if grep -Eq \
            'HAVE_X86ASM=yes' \
            ffbuild/config.mak \
            2>/dev/null
        then
            echo "[ERROR] ARM64 configure 启用了 X86ASM"
            exit 1
        fi

    fi

    # --------------------------------------------------------
    # armv7
    # --------------------------------------------------------

    if [ "$ARCH" = "armeabi-v7a" ]; then

        if grep -Eq \
            'CONFIG_X86(_64)?[[:space:]]+1' \
            config.h \
            2>/dev/null
        then
            echo "[ERROR] ARMv7 configure 被错误识别为 X86/X86_64"
            exit 1
        fi

        if grep -Eq \
            'HAVE_X86ASM=yes' \
            ffbuild/config.mak \
            2>/dev/null
        then
            echo "[ERROR] ARMv7 configure 启用了 X86ASM"
            exit 1
        fi

    fi

    echo ">>> configure 架构检查完成"
}

# ============================================================
# 9. 检查 ARM 架构是否出现 X86 对象
# ============================================================

check_x86_objects()
{
    local ARCH="$1"

    if [ "$ARCH" = "x86_64" ]; then
        return 0
    fi

    echo
    echo ">>> 检查 ARM 架构是否存在 X86 libavcodec 对象..."

    # --------------------------------------------------------
    # Makefile 静态检查
    # --------------------------------------------------------

    if grep -nE \
        'libavcodec/x86|libavcodec/x86_64|x86/.*\.o|x86_64/.*\.o' \
        libavcodec/Makefile \
        2>/dev/null
    then

        echo
        echo "[WARNING] libavcodec/Makefile 中出现 X86 字符串。"
        echo "这不一定意味着最终会编译 X86 对象。"
        echo

    fi

    # --------------------------------------------------------
    # Makefile dry-run
    # --------------------------------------------------------

    echo ">>> 检查实际 Makefile 展开的对象..."

    local TMP_LOG

    TMP_LOG="$(mktemp)"

    if make -n libavcodec/libavcodec.so >"$TMP_LOG" 2>&1; then

        if grep -iE \
            'libavcodec/(x86|x86_64)/.*\.(o|lo)' \
            "$TMP_LOG"
        then

            echo
            echo "[ERROR] 检测到 ARM 架构正在尝试编译/链接 X86 对象！"
            echo
            echo "相关内容："

            grep -iE \
                'libavcodec/(x86|x86_64)/.*\.(o|lo)' \
                "$TMP_LOG" || true

            rm -f "$TMP_LOG"

            echo
            echo ">>> 构建已停止。"

            exit 1

        fi

    fi

    rm -f "$TMP_LOG"

    echo ">>> 未发现 ARM -> X86 libavcodec 对象链接。"
}

# ============================================================
# 10. 构建选定 ABI
# ============================================================

for ARCH in "${SELECTED_ARCHS[@]}"; do

    echo
    echo "======================================================="
    echo ">>> 开始构建架构：$ARCH"
    echo "======================================================="
    echo

    # --------------------------------------------------------
    # 清理宿主环境
    # --------------------------------------------------------

    clean_environment

    # --------------------------------------------------------
    # 架构变量
    # --------------------------------------------------------

    case "$ARCH" in

        arm64-v8a)

            FF_ARCH="aarch64"
            FF_CPU="armv8-a"
            TARGET="aarch64-linux-android"

            EXTRA_FLAGS="-march=armv8-a+crypto+crc -mcpu=generic"

            ASM_OPTS=(
                "--disable-x86asm"
            )

            MAKE_ARGS=(
                "X86ASM="
            )

            ;;

        armeabi-v7a)

            FF_ARCH="arm"
            FF_CPU="armv7-a"
            TARGET="armv7a-linux-androideabi"

            EXTRA_FLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=softfp"

            ASM_OPTS=(
                "--disable-x86asm"
            )

            MAKE_ARGS=(
                "X86ASM="
            )

            ;;

        x86_64)

            FF_ARCH="x86_64"
            FF_CPU="x86-64"
            TARGET="x86_64-linux-android"

            EXTRA_FLAGS="-march=x86-64"

            ASM_OPTS=(
                "--enable-asm"
                "--enable-x86asm"
                "--x86asmexe=nasm"
            )

            MAKE_ARGS=()

            ;;

        *)

            echo "[ERROR] 未知架构：$ARCH"
            exit 1

            ;;

    esac

    # --------------------------------------------------------
    # 当前 ABI 独立目录
    # --------------------------------------------------------

    DEPS_DIR="$ANDROID_DEPS_ROOT/$ARCH"
    PREFIX="$BUILD_DIR_ROOT/$ARCH"

    mkdir -p "$PREFIX"

    # --------------------------------------------------------
    # 检查当前 ABI 依赖
    # --------------------------------------------------------

    check_deps "$DEPS_DIR"

    # --------------------------------------------------------
    # PATH
    #
    # MSYS2 放在前面，避免 NDK/bin 中的 DLL 污染 MSYS2 GCC。
    #
    # NDK 工具通过绝对路径调用。
    # --------------------------------------------------------

    export PATH="/mingw64/bin:$LLVM_BIN:/usr/bin:/bin"

    export HOSTCC="/mingw64/bin/gcc"
    export HOSTCXX="/mingw64/bin/g++"

    # --------------------------------------------------------
    # Android LLVM 工具
    # --------------------------------------------------------

    export CC="$LLVM_BIN/${TARGET}${API_LEVEL}-clang"
    export CXX="$LLVM_BIN/${TARGET}${API_LEVEL}-clang++"

    export AR="$LLVM_BIN/llvm-ar"
    export NM="$LLVM_BIN/llvm-nm"
    export RANLIB="$LLVM_BIN/llvm-ranlib"
    export STRIP="$LLVM_BIN/llvm-strip"

    export OBJCOPY="$LLVM_BIN/llvm-objcopy"
    export OBJDUMP="$LLVM_BIN/llvm-objdump"
    export READELF="$LLVM_BIN/llvm-readelf"

    # --------------------------------------------------------
    # 检查 NDK 工具
    # --------------------------------------------------------

    for TOOL in \
        "$CC" \
        "$CXX" \
        "$AR" \
        "$NM" \
        "$RANLIB" \
        "$STRIP"
    do

        if [ ! -f "$TOOL" ]; then

            echo "[ERROR] 找不到工具："
            echo "        $TOOL"

            exit 1

        fi

    done

    # --------------------------------------------------------
    # x86_64 检查 NASM
    # --------------------------------------------------------

    if [ "$ARCH" = "x86_64" ]; then

        if ! command -v nasm >/dev/null 2>&1; then

            echo "[ERROR] x86_64 Android 构建需要 NASM。"
            echo
            echo "请先确认："
            echo "  which nasm"

            exit 1

        fi

        echo ">>> NASM：$(command -v nasm)"

    fi

    # --------------------------------------------------------
    # ARM 架构彻底禁用 NASM/YASM/X86ASM
    # --------------------------------------------------------

    if [ "$ARCH" != "x86_64" ]; then

        unset NASM
        unset YASM
        unset X86ASM

        export NASM=""
        export YASM=""
        export X86ASM=""

    else

        unset X86ASM

    fi

    # --------------------------------------------------------
    # pkg-config
    # --------------------------------------------------------

    export PKG_CONFIG_LIBDIR="$DEPS_DIR/lib/pkgconfig"
    export PKG_CONFIG_PATH=""

    unset PKG_CONFIG_SYSROOT_DIR

    if ! command -v pkg-config >/dev/null 2>&1; then

        echo "[ERROR] 找不到 pkg-config"
        exit 1

    fi

    echo ">>> pkg-config：$(command -v pkg-config)"

    # --------------------------------------------------------
    # 验证当前 ABI 的 pkg-config
    # --------------------------------------------------------

    echo
    echo ">>> 检查 DAVS2 / UAVS3D pkg-config..."

    if ! pkg-config --exists "davs2 >= 1.6.0"; then
        echo "[ERROR] davs2 >= 1.6.0 检测失败"
        exit 1
    fi

    if ! pkg-config --exists "uavs3d >= 1.1.89"; then
        echo "[ERROR] uavs3d >= 1.1.89 检测失败"
        exit 1
    fi

    echo "    davs2  : $(pkg-config --modversion davs2)"
    echo "    uavs3d : $(pkg-config --modversion uavs3d)"

    # --------------------------------------------------------
    # 编译旗标
    # --------------------------------------------------------

    EXTRA_CFLAGS="\
$OPT_FLAGS \
$EXTRA_FLAGS \
-I$DEPS_DIR/include \
-fPIC \
-fomit-frame-pointer"

    # --------------------------------------------------------
    # 显示环境
    # --------------------------------------------------------

    show_toolchain

    echo ">>> ARCH      = $FF_ARCH"
    echo ">>> CPU       = $FF_CPU"
    echo ">>> TARGET    = $TARGET"
    echo ">>> DEPS_DIR  = $DEPS_DIR"
    echo ">>> PREFIX    = $PREFIX"
    echo ">>> CFLAGS    = $EXTRA_CFLAGS"
    echo

    # ========================================================
    # 11. 只清理当前 ABI 的 FFmpeg 输出
    # ========================================================

    echo ">>> 清理当前 ABI 输出：$PREFIX"

    rm -rf -- "$PREFIX"
    mkdir -p -- "$PREFIX"

    # ========================================================
    # 12. 清理 FFmpeg 上一次架构配置
    # ========================================================

    echo
    echo ">>> 清理 FFmpeg 上一次架构配置..."

    make distclean >/dev/null 2>&1 || true

    # distclean 后再次确保生成配置文件已经清除
    rm -rf \
        ffbuild/.config \
        ffbuild/config.log \
        ffbuild/config.mak \
        ffbuild/config.h \
        config.h \
        config.mak \
        config.asm

    # --------------------------------------------------------
    # 检查源码关键文件仍然存在
    # --------------------------------------------------------

    if [ ! -f ffbuild/common.mak ]; then
        echo "[ERROR] make distclean 后 ffbuild/common.mak 消失！"
        echo "        这不是正常行为。"
        exit 1
    fi

    # ========================================================
    # 13. configure
    # ========================================================

    echo
    echo ">>> 开始 configure：$ARCH"
    echo

    cfg_options=(

        # ----------------------------------------------------
        # Target
        # ----------------------------------------------------

        --target-os=android
        --arch="$FF_ARCH"
        --cpu="$FF_CPU"

        --prefix="$PREFIX"

        --cross-prefix="$LLVM_BIN/llvm-"
        --pkg-config=pkg-config

        --cc="$CC"
        --cxx="$CXX"

        --ar="$AR"
        --nm="$NM"
        --ranlib="$RANLIB"
        --strip="$STRIP"

        --enable-cross-compile

        # ----------------------------------------------------
        # CPU
        # ----------------------------------------------------

        --enable-runtime-cpudetect
        --enable-optimizations

        # ----------------------------------------------------
        # Core libraries
        # ----------------------------------------------------

        --enable-avcodec
        --enable-avformat
        --enable-avutil
        --enable-swresample
        --enable-swscale
        --enable-avfilter
        --enable-avdevice

        # ----------------------------------------------------
        # Formats / protocols
        # ----------------------------------------------------

        --enable-network
        --enable-protocols
        --enable-parsers
        --enable-demuxers
        --enable-filters
        --enable-bsfs
        --enable-pthreads
        --enable-zlib

        # ----------------------------------------------------
        # 国产标准
        # ----------------------------------------------------

        --enable-libdavs2
        --enable-libuavs3d

        # ----------------------------------------------------
        # Android
        # ----------------------------------------------------

        --enable-jni
        --enable-mediacodec

        # ----------------------------------------------------
        # Shared library
        # ----------------------------------------------------

        --enable-shared
        --disable-static

        --enable-gpl
        --enable-nonfree
        --enable-version3

        # ----------------------------------------------------
        # 瘦身
        # ----------------------------------------------------

        --disable-debug
        --disable-doc
        --disable-programs

        --disable-encoders
        --disable-muxers

        --enable-stripping

        # ----------------------------------------------------
        # 编译参数
        # ----------------------------------------------------

        --extra-cflags="$EXTRA_CFLAGS"
        --extra-ldflags="-L$DEPS_DIR/lib"
        --extra-libs="-lm -lz -ldl"
    )

    # --------------------------------------------------------
    # 架构特定 configure
    # --------------------------------------------------------

    for asm_flag in "${ASM_OPTS[@]}"; do
        cfg_options+=("$asm_flag")
    done

    # --------------------------------------------------------
    # ARM 架构明确禁止 X86
    # --------------------------------------------------------

    if [ "$ARCH" != "x86_64" ]; then

        cfg_options+=(
            --disable-x86asm
        )

    fi

    # ========================================================
    # 14. 执行 configure
    # ========================================================

    ./configure "${cfg_options[@]}"

    # ========================================================
    # 15. configure 检查
    # ========================================================

    verify_config "$ARCH"

    # ========================================================
    # 16. ARM 架构检查 X86 对象
    # ========================================================

    check_x86_objects "$ARCH"

    # ========================================================
    # 17. 保存 configure 日志
    # ========================================================

    CONFIG_LOG="$PREFIX/configure-$ARCH.log"

    if [ -f ffbuild/config.log ]; then

        cp -f \
            ffbuild/config.log \
            "$CONFIG_LOG"

    elif [ -f config.log ]; then

        cp -f \
            config.log \
            "$CONFIG_LOG"

    fi

    echo
    echo ">>> configure 日志：$CONFIG_LOG"
    echo

    # ========================================================
    # 18. 编译
    # ========================================================

    NPROC="$(nproc 2>/dev/null || echo 4)"

    echo
    echo "======================================================="
    echo ">>> 开始编译 $ARCH"
    echo ">>> 并行线程：$NPROC"
    echo "======================================================="
    echo

    make -j"$NPROC" "${MAKE_ARGS[@]}"

    # ========================================================
    # 19. 安装
    # ========================================================

    echo
    echo ">>> 安装 $ARCH"
    echo

    make install

    # ========================================================
    # 20. 检查最终输出
    # ========================================================

    echo
    echo ">>> 检查 $ARCH 最终输出"
    echo

    if [ ! -d "$PREFIX" ]; then

        echo "[ERROR] PREFIX 不存在："
        echo "$PREFIX"

        exit 1

    fi

	LIBAVCODEC_SO="$(find "$PREFIX/lib" -maxdepth 1 -type f -name 'libavcodec.so*' | head -n 1)"

    if [ -z "$LIBAVCODEC_SO" ]; then

        echo "[ERROR] 未找到 libavcodec.so"
		echo "        $PREFIX/lib/"

        exit 1

    fi

    echo ">>> libavcodec OK:"
	echo "    $LIBAVCODEC_SO"

    # --------------------------------------------------------
    # 记录库文件
    # --------------------------------------------------------

    find "$PREFIX/lib" \
        -maxdepth 1 \
        -type f \
        -printf "    %f\n" \
        2>/dev/null \
        | sort || true

    # ========================================================
    # 21. 当前 ABI 完成
    # ========================================================

    echo
    echo "======================================================="
    echo ">>> $ARCH 构建完成"
    echo "======================================================="
    echo
    echo "FFmpeg 输出："
    echo "$PREFIX"
    echo
    echo "依赖目录："
    echo "$DEPS_DIR"
    echo

done

# ============================================================
# 22. 全部完成
# ============================================================

echo
echo "======================================================="
echo " Android FFmpeg 构建完成"
echo "======================================================="
echo

echo "本次构建 ABI："

for ARCH in "${SELECTED_ARCHS[@]}"; do
    echo "  ✓ $ARCH"
done

echo
echo "输出目录："
echo "  $BUILD_DIR_ROOT"
echo