#!/bin/bash

# FFmpeg AVS 环境一键部署脚本 (本地源码编译版)
# 适用环境：MSYS2 MINGW64
# 逻辑：安装基础依赖 -> 编译本地已修补的 AVS 库

echo "=== [1/5] 正在安装基础开发工具链 ==="
# 仅安装必需的二进制包，不更改镜像设置
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-toolchain yasm nasm make pkg-config diffutils git cmake \
    mingw-w64-x86_64-x264 mingw-w64-x86_64-x265 mingw-w64-x86_64-libvpx \
    mingw-w64-x86_64-lame mingw-w64-x86_64-opus mingw-w64-x86_64-libvorbis \
    mingw-w64-x86_64-SDL2 mingw-w64-x86_64-libass mingw-w64-x86_64-zimg \
    mingw-w64-x86_64-libplacebo mingw-w64-x86_64-vulkan-loader mingw-w64-x86_64-shaderc \
    mingw-w64-x86_64-ffnvcodec-headers mingw-w64-x86_64-libvpl \
    mingw-w64-x86_64-openssl mingw-w64-x86_64-libxml2 mingw-w64-x86_64-libbluray

BASE_DIR="/e/BaiduNetdiskDownload/FFmpegProject"
cd $BASE_DIR

echo "=== [2/5] 正在从本地源码编译 libdavs2 (10-bit) ==="
if [ -d "davs2" ]; then
    cd davs2/build/linux
    ./configure --prefix=/mingw64 --enable-shared --bit-depth=10
    make -j$(nproc 2>/dev/null || echo 4) && make install
else
    echo "Error: 找不到 davs2 目录，请确保源码已解压到 $BASE_DIR/davs2"
fi

echo "=== [3/5] 正在从本地源码编译 libxavs2 ==="
cd $BASE_DIR
if [ -d "xavs2" ]; then
    cd xavs2/build/linux
    ./configure --prefix=/mingw64 --enable-shared
    make -j$(nproc 2>/dev/null || echo 4) && make install
    # --- 自动生成 xavs2.pc 防止 FFmpeg 探测失败 ---
    cat > /mingw64/lib/pkgconfig/xavs2.pc <<EOF
prefix=/mingw64
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include
Name: xavs2
Description: AVS2 encoder library
Version: 1.3.0
Libs: -L\${libdir} -lxavs2 -lm -lpthread
Cflags: -I\${includedir}
EOF
else
    echo "Error: 找不到 xavs2 目录"
fi

echo "=== [4/5] 正在从本地源码编译 libuavs3d (AVS3 解码) ==="
cd $BASE_DIR
if [ -d "uavs3d" ]; then
    cd uavs3d
    rm -rf build_msys && mkdir build_msys && cd build_msys
    cmake .. -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX=/mingw64 -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc 2>/dev/null || echo 4)
    # 手动安装关键资产
    cp -v source/libuavs3d.a /mingw64/lib/
    cp -v ../source/decoder/uavs3d.h /mingw64/include/
    [ -f "../uavs3d.pc" ] && cp -v ../uavs3d.pc /mingw64/lib/pkgconfig/
else
    echo "Error: 找不到 uavs3d 目录"
fi

echo "=== [5/5] 正在从本地源码编译 libuavs3e (AVS3 编码) ==="
cd $BASE_DIR
if [ -d "uavs3e" ]; then
    cd uavs3e
    rm -rf build_msys && mkdir build_msys && cd build_msys
    cmake .. -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX=/mingw64 -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc 2>/dev/null || echo 4)
    # 手动安装关键资产
    cp -v src/libuavs3e.a /mingw64/lib/
    cp -v ../inc/uavs3e.h /mingw64/include/
    [ -f "../uavs3e.pc" ] && cp -v ../uavs3e.pc /mingw64/lib/pkgconfig/
else
    echo "Error: 找不到 uavs3e 目录"
fi

echo "-------------------------------------------------------"
echo "本地 AVS 依赖环境一键编译部署完成！"
echo "-------------------------------------------------------"
