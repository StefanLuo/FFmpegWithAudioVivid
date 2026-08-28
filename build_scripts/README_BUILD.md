# FFmpeg Multi-platform Build Scripts

These scripts are designed to simplify the process of compiling FFmpeg for different platforms.

## Prerequisites

### For Linux Builds
- Standard build tools (`gcc`, `make`, `pkg-config`).

### For Android Builds
- Android NDK (update `NDK_PATH` in `env.sh`).
- Linux, macOS, or Windows (MSYS2) host.

### For Windows Builds (Cross-compilation on Linux)
- `mingw-w64` toolchain (e.g., `sudo apt-get install mingw-w64`).
- Ensure `x86_64-w64-mingw32-gcc` is in your PATH.

### For Windows Builds (Native on Windows)
- **MSYS2** with `mingw-w64-x86_64-toolchain` installed.
- Ensure your `MSYS2_PATH` in `env.sh` matches your installation (e.g., `D:/msys64`).
- Open the **MSYS2 MINGW64** terminal to run the scripts.

## Usage

1.  **Configure Environment**: Edit `build_scripts/env.sh` to set your paths and common FFmpeg flags.
2.  **Run Scripts**:
    - Native Linux: `./build_scripts/build_linux.sh`
    - Windows x64: `./build_scripts/build_windows.sh`
    - Android: Edit `build_scripts/build_android.sh` to select architectures, then run it.

## Troubleshooting Windows Dynamic Libraries

If dynamic libraries (`.dll`) are not generated when cross-compiling on Linux, ensure:
1.  `--target-os=mingw32` is set.
2.  `--enable-shared` is enabled.
3.  The `mingw-w64` toolchain is correctly installed and reachable via the prefix.
4.  The script uses `--enable-w32threads` to satisfy Windows threading requirements.
