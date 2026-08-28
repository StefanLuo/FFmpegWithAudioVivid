#!/bin/bash

# FFmpeg AVS 项目全量清理脚本
# 功能：清除 AVS1/2/3 第三方库及 FFmpeg 的所有编译中间件与产物
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== FFmpeg AVS 项目全量清理 ==="
echo

echo "=== [1/5] 正在清理 FFmpeg_n9.0.1 ==="
if [ -d "$ROOT_DIR/FFmpeg_n9.0.1" ]; then
    cd "$ROOT_DIR/FFmpeg_n9.0.1" || exit 1
    # FFmpeg 的 distclean 非常彻底，会清理 config 文件
    make distclean >/dev/null 2>&1 || true
    rm -rf -- "$ROOT_DIR"/*_build
    cd "$ROOT_DIR" || exit 1
fi

echo "=== [2/5] 正在清理 libdavs2 ==="
if [ -d "$ROOT_DIR/davs2" ]; then
	rm -f "$ROOT_DIR"/davs2/*.a
    rm -rf -- "$ROOT_DIR"/davs2/build/android_*
    if [ -d "$ROOT_DIR/davs2/build/linux" ]; then
        cd "$ROOT_DIR/davs2/build/linux" || exit 1
        make distclean >/dev/null 2>&1 || true
        rm -f config.h config.log config.mak davs2.pc conftest.c conftest.log
		rm -rf -- common test
        cd "$ROOT_DIR" || exit 1
    fi
fi

echo "=== [3/5] 正在清理 libxavs2 ==="
if [ -d "$ROOT_DIR/xavs2" ]; then
	rm -f "$ROOT_DIR"/xavs2/*.a
    rm -rf -- "$ROOT_DIR"/xavs2/build/android_*
    if [ -d "$ROOT_DIR/xavs2/build/linux" ]; then
        cd "$ROOT_DIR/xavs2/build/linux" || exit 1
        make distclean >/dev/null 2>&1 || true
        rm -f config.h config.log config.mak xavs2.pc conftest.c conftest.log
		rm -rf -- common encoder test
        cd "$ROOT_DIR" || exit 1
    fi
fi

echo "=== [4/5] 正在清理 libuavs3d ==="
if [ -d "$ROOT_DIR/uavs3d" ]; then
    # CMake 项目直接删除构建文件夹即可
    rm -rf -- "$ROOT_DIR"/uavs3d/build_*
    rm -f "$ROOT_DIR"/uavs3d/uavs3d.pc
fi

echo "=== [5/5] 正在清理 libuavs3e ==="
if [ -d "$ROOT_DIR/uavs3e" ]; then
    rm -rf -- "$ROOT_DIR"/uavs3e/build_*
    rm -f uavs3e/uavs3e.pc
fi

# FFmpeg Android 构建产物
rm -rf -- "$ROOT_DIR/android_build"

# 第三方依赖编译中间文件
rm -rf -- "$ROOT_DIR/android_build_deps"

# 第三方依赖最终产物
rm -rf -- "$ROOT_DIR/android_deps"

rm -f "$ROOT_DIR"/*.tmp "$ROOT_DIR"/log_dec.txt

echo "-------------------------------------------------------"
echo "全量清理完成！源码树已恢复到最纯净状态。"
echo "-------------------------------------------------------"
