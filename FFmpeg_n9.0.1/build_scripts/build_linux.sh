#!/bin/bash

source ./build_scripts/env.sh

echo "Building for Linux..."

PREFIX=$LINUX_BUILD_DIR
mkdir -p $PREFIX

./configure \
    --prefix=$PREFIX \
    --enable-shared \
    --disable-static \
    $COMMON_FF_CFG_FLAGS

make clean
make -j$(nproc 2>/dev/null || echo 4)
make install
