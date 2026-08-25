# FFmpeg Windows (MSYS2 MINGW64) 满配编译指南

本指南详细记录了如何编译出一个包含 **Vulkan、全套硬件加速（NVENC/QSV/AMF/D3D11）、以及最广泛第三方格式支持** 的 FFmpeg 版本。

---

## 1. 环境准备 (MSYS2)

请确保安装了 [MSYS2](https://www.msys2.org/) 并使用 **MINGW64** 终端执行以下指令。

### 1.1 安装满配开发工具链与依赖
这些包涵盖了 FFmpeg 几乎所有的可选特性，无论目前是否用到，建议全部安装以保证编译器的最大探测能力。

```bash
# 更新系统
pacman -Syu --noconfirm

# 基础工具
pacman -S --noconfirm mingw-w64-x86_64-toolchain yasm nasm make pkg-config diffutils git cmake

# 视频库 (x264, x265, AV1, VP9, etc.)
pacman -S --noconfirm mingw-w64-x86_64-x264 mingw-w64-x86_64-x265 mingw-w64-x86_64-libvpx \
    mingw-w64-x86_64-dav1d mingw-w64-x86_64-aom mingw-w64-x86_64-openjpeg2 mingw-w64-x86_64-xvidcore

# 音频库 (MP3, Opus, Vorbis, etc.)
pacman -S --noconfirm mingw-w64-x86_64-lame mingw-w64-x86_64-opus mingw-w64-x86_64-libvorbis \
    mingw-w64-x86_64-speex mingw-w64-x86_64-opencore-amr mingw-w64-x86_64-libsoxr

# 图形、滤镜与字幕
pacman -S --noconfirm mingw-w64-x86_64-SDL2 mingw-w64-x86_64-freetype mingw-w64-x86_64-fontconfig \
    mingw-w64-x86_64-fribidi mingw-w64-x86_64-libass mingw-w64-x86_64-libwebp mingw-w64-x86_64-zimg

# 硬件加速与 Vulkan 渲染核心
pacman -S --noconfirm mingw-w64-x86_64-vulkan-headers mingw-w64-x86_64-vulkan-loader \
    mingw-w64-x86_64-shaderc mingw-w64-x86_64-libplacebo mingw-w64-x86_64-ffnvcodec-headers \
    mingw-w64-x86_64-libvpl mingw-w64-x86_64-amf-headers mingw-w64-x86_64-spirv-headers

# 网络安全与系统支持
pacman -S --noconfirm mingw-w64-x86_64-openssl mingw-w64-x86_64-srt mingw-w64-x86_64-rtmpdump \
    mingw-w64-x86_64-libxml2 mingw-w64-x86_64-libbluray mingw-w64-x86_64-xz mingw-w64-x86_64-bzip2 mingw-w64-x86_64-zlib
```

---

## 2. 源码补丁说明

本项目的脚本已自动修复了以下 Windows 环境下的源码 Bug：

1.  **fmemopen 缺失**：增加了 `compat/fmemopen_win.h` 补丁，并修改了 `libavcodec/avs3adec.c`。
2.  **cnn_layer.c 类型匹配**：修正了 AVS3A 解码器中的 `const float **` 编译器报错。

---

## 3. 执行编译

已将脚本拆分为**静态构建**和**动态构建**两种模式，请根据需求选择：

### 3.1 纯静态构建 (生成独立可执行文件)
*   **脚本**：`build_scripts/build_windows_static.sh`
*   **产物目录**：`windows_build_static/bin`
*   **特点**：生成不依赖 FFmpeg DLL 的 `ffmpeg.exe`。脚本会自动清理不需要的 `include` 和 `lib` 目录。
*   **修复**：已加入 `-lstdc++` 等参数，解决 `libbluray` 等静态链接报错。

### 3.2 动态库构建 (用于开发)
*   **脚本**：`build_scripts/build_windows_shared.sh`
*   **产物目录**：`windows_build_shared`
*   **特点**：生成 `avcodec-xx.dll`、导入库（`.lib`）和头文件，并自动补全所有缺失的第三方依赖 DLL（如 `libdovi.dll`, `libsnappy.dll` 等）。

---

## 4. 运行与验证

### 4.1 独立运行
进入 `windows_build/bin`，直接运行 `ffmpeg.exe`。它现在不需要任何外部环境变量即可在任意文件夹下运行。

### 4.2 性能验证
使用 D3D11 硬件加速解码示例：
```bash
ffmpeg.exe -hwaccel d3d11va -i input.mp4 -f null -
```

### 4.3 播放验证 (ffplay)
如果播放包含多个声道的专业音频，请强制下混：
```bash
ffplay.exe -ac 2 test.mp4
```

---

## 5. 常见问题排查 (0xc000007b)
如果遇到该错误，说明环境中混入了 32 位的 DLL。
*   **解决**：删除 `windows_build` 目录，确保在 **MINGW64** 终端中重新运行脚本。
*   **原理**：本项目的脚本已强制从 `/mingw64/bin` 路径拷贝 64 位 DLL，通常能自动避免此问题。
