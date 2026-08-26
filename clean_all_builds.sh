#!/bin/bash

# FFmpeg AVS 项目全量清理脚本
# 功能：清除 AVS1/2/3 第三方库及 FFmpeg 的所有编译中间件与产物

echo "=== [1/5] 正在清理 FFmpeg_n9.0.1 ==="
if [ -d "FFmpeg_n9.0.1" ]; then
    cd FFmpeg_n9.0.1
    # FFmpeg 的 distclean 非常彻底，会清理 config 文件
    make distclean 2>/dev/null
    rm -rf windows_build_shared windows_build_static android_build linux_build
    cd ..
fi

echo "=== [2/5] 正在清理 libdavs2 ==="
if [ -d "davs2/build/linux" ]; then
    cd davs2/build/linux
    # 注意：davs2 的 distclean 只相当于 clean，所以我们手动补齐删除配置文件的逻辑
    make distclean 2>/dev/null
    rm -f config.h config.log config.mak davs2.pc conftest.c conftest.log
    cd ../../..
fi

echo "=== [3/5] 正在清理 libxavs2 ==="
if [ -d "xavs2/build/linux" ]; then
    cd xavs2/build/linux
    make distclean 2>/dev/null
    rm -f config.h config.log config.mak xavs2.pc conftest.c conftest.log
    cd ../../..
fi

echo "=== [4/5] 正在清理 libuavs3d ==="
if [ -d "uavs3d" ]; then
    # CMake 项目直接删除构建文件夹即可
    rm -rf uavs3d/build_msys
    rm -f uavs3d/uavs3d.pc
fi

echo "=== [5/5] 正在清理 libuavs3e ==="
if [ -d "uavs3e" ]; then
    rm -rf uavs3e/build_msys
    rm -f uavs3e/uavs3e.pc
fi

# 清理根目录产生的临时文件
rm -f *.tmp log_dec.txt

echo "-------------------------------------------------------"
echo "全量清理完成！源码树已恢复到最纯净状态。"
echo "-------------------------------------------------------"
