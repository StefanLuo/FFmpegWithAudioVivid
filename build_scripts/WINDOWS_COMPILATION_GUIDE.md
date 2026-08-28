# FFmpeg Windows (MSYS2 MINGW64) 满配编译指南

本指南详细记录了如何编译出一个包含 **Vulkan、AVS+/AVS2/AVS3 (全家桶)、全套硬件加速、以及 Audio Vivid** 支持的 FFmpeg 版本。

---

## 1. 环境准备 (MSYS2)

请确保安装了 [MSYS2](https://www.msys2.org/) 并使用 **MINGW64** 终端。

### 1.1 基础工具链
```bash
pacman -Syu --noconfirm
pacman -S --noconfirm mingw-w64-x86_64-toolchain yasm nasm make pkg-config diffutils git cmake
```

### 1.2 编译 AVS+/AVS2 解码库 (libdavs2) - 支持 10-bit
**关键点**：必须开启 10-bit 支持，否则 4K AVS2 视频将只有声音没有画面。
```bash
cd /e/BaiduNetdiskDownload/FFmpegProject/
git clone https://github.com/pkuvcl/davs2.git
cd davs2/build/linux
# 源码补丁：如果报错 'cannot convert pel_t* to uint8_t*'，需修改 common/decoder.cc 进行强制类型转换
./configure --prefix=/mingw64 --enable-shared --bit-depth=10
make -j$(nproc) && make install
```

### 1.3 编译 AVS2 视频编码器 (libxavs2)
```bash
cd /e/BaiduNetdiskDownload/FFmpegProject/
git clone https://github.com/pkuvcl/xavs2.git
cd xavs2/build/linux
# 源码补丁：如果报错 'incompatible pointer types'，需修改 encoder/encoder.c 将线程函数参数改为 (void *arg)
./configure --prefix=/mingw64 --enable-shared
make -j$(nproc) && make install

# Pkg-config 修复：如果 FFmpeg 找不到 xavs2，手动在 /mingw64/lib/pkgconfig/ 创建 xavs2.pc
```

### 1.4 编译 AVS3 视频解码库 (libuavs3d)
```bash
cd /e/BaiduNetdiskDownload/FFmpegProject/
git clone https://github.com/uavs3/uavs3d.git
cd uavs3d && mkdir -p build_msys && cd build_msys
cmake .. -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX=/mingw64 -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) && make install
# 手动拷贝：cp source/libuavs3d.a /mingw64/lib/
```

---

## 2. 深度功能增强 (源码补丁)

本项目对 FFmpeg 官方源码进行了如下深度深度优化，解决了多项“疑难杂症”：

1.  **Audio Vivid (AV3A) 增强**：
    *   **指针重置**：修复了解码首帧时指针偏移导致的“有识别无声音”问题。
    *   **模型寻轨**：支持自动在程序同级目录加载 `model.bin`。
    *   **全景声映射**：为 12 通道 (7.1.4) 提供了高精度空间布局标签。
2.  **MPEG-TS 封装自动纠错**：
    *   **特征码嗅探**：针对某些 TS 流将 AVS3 音频误标为 `0x04 (MP3)` 的问题，增加了基于 `0xFFF2` 指纹的强制识别逻辑。
    *   **上下文刷新**：识别纠正后自动触发播放器重新初始化，确保声音立刻输出。
3.  **Vulkan 渲染固化**：
    *   **实心化链接**：强制硬链接 Vulkan 加载器，解决了动态编译下提示“Function not implemented”并崩溃的问题。

---

## 3. 执行编译

*   **动态构建 (DLL)**：`./build_scripts/build_windows_shared.sh`
*   **静态构建 (EXE)**：`./build_scripts/build_windows_static.sh`

---

## 4. 验证与播放

### AVS3 4K Vivid (全量支持)
```bash
ffplay.exe -hwaccel d3d11va test_4k_vivid.ts
```

### 伪装成 MP3 的 AVS3 码流 (自动识别)
```bash
# 现在无需手动指定 -acodec，系统会自动发现 0xFFF2 特征
ffplay.exe 05.HEVC.AUDIOVivid.ts
```

### AVS2 4K 10-bit (高色深支持)
```bash
ffplay.exe "AVS2_10bit_Sample.ts"
```
