# Windows Cross-Compilation Guide

Cross-compile open-broadcast-encoder for Windows x86_64 from Linux (Arch/CachyOS) using MinGW-w64.

## Prerequisites

### System packages

```sh
sudo pacman -S mingw-w64-gcc zip innoextract
paru -S nsis
```

### vcpkg

```sh
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
curl -fsSL https://github.com/microsoft/vcpkg-tool/releases/download/2026-05-27/vcpkg-glibc \
  -o ~/vcpkg/vcpkg && chmod +x ~/vcpkg/vcpkg
export VCPKG_ROOT=~/vcpkg
```

### NDI SDK for Windows

Download **NDI 6 SDK** (Windows) from [ndi.video/for-developers](https://ndi.video/for-developers/) and extract it:

```sh
mkdir -p ~/opt/ndi-sdk-windows
innoextract -d ~/opt/ndi-sdk-windows "NDI 6 SDK.exe"
export NDI_SDK_DIR=~/opt/ndi-sdk-windows/app
```

### GStreamer 1.28 MinGW packages

Download the required MSYS2 packages and extract them into a single prefix:

```sh
mkdir -p ~/opt/gstreamer-mingw && cd ~/opt/gstreamer-mingw

for pkg in \
  mingw-w64-x86_64-gstreamer-1.28.3-1-any \
  mingw-w64-x86_64-gst-plugins-base-1.28.3-1-any \
  mingw-w64-x86_64-glib2-2.84.1-1-any \
  mingw-w64-x86_64-libffi-3.4.8-1-any \
  mingw-w64-x86_64-pcre2-10.45-1-any \
  mingw-w64-x86_64-orc-0.4.41-1-any \
  mingw-w64-x86_64-gettext-runtime-0.22.5-2-any; do
  curl -fsSL -o "${pkg}.pkg.tar.zst" \
    "https://mirror.msys2.org/mingw/mingw64/${pkg}.pkg.tar.zst"
  tar --use-compress-program=zstd -xf "${pkg}.pkg.tar.zst"
done

export GSTREAMER_MINGW_PREFIX=~/opt/gstreamer-mingw/mingw64
```

Patch the pkg-config prefix entries to use absolute host paths (required once after extraction):

```sh
PKGDIR="$GSTREAMER_MINGW_PREFIX/lib/pkgconfig"
for f in "$PKGDIR"/*.pc; do
  sed -i "s|^prefix=/mingw64|prefix=$GSTREAMER_MINGW_PREFIX|" "$f"
done
```

### MinGW Windows header case-fix

Linux has a case-sensitive filesystem; MinGW-w64 ships headers in lowercase but some
code includes them with Windows-canonical casing (e.g. `<Winsock2.h>`). Create symlinks
once:

```sh
mkdir -p ~/Projects/open-broadcast/open-broadcast-encoder/build-external/mingw-case-fix/include
cd ~/Projects/open-broadcast/open-broadcast-encoder/build-external/mingw-case-fix/include
for f in /usr/x86_64-w64-mingw32/include/*.h; do
  cap="$(python3 -c "s='$(basename $f)'; print(s[0].upper()+s[1:])")"
  [ "$cap" != "$(basename $f)" ] && ln -sf "$f" "$cap" 2>/dev/null || true
done
```

## Build

```sh
cd ~/Projects/open-broadcast/open-broadcast-encoder

export VCPKG_ROOT=~/vcpkg
export NDI_SDK_DIR=~/opt/ndi-sdk-windows/app
export GSTREAMER_MINGW_PREFIX=~/opt/gstreamer-mingw/mingw64

cmake -S . -B build/cross-win64 --preset cross-win64 -DNDI_SDK_DIR="$NDI_SDK_DIR"
cmake --build build/cross-win64
```

The Windows executable is at `build/cross-win64/open-broadcast-encoder.exe`.

> **Note:** vcpkg appends a `powershell.exe` post-build step that fails on Linux — this
> is harmless. If the build reports exit code 127 but the `.exe` exists, the link
> succeeded.

## Package as NSIS installer

```sh
cd build/cross-win64
cpack -G NSIS --config CPackConfig.cmake
```

Output: `build/cross-win64/Open Broadcast Encoder-<version>-win64.exe`

The installer places the app under `%PROGRAMFILES64%\Open Broadcast Encoder\` and
creates a Start Menu shortcut. It includes the MinGW runtime DLLs
(`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`).

> **Note on `libssp-0.dll`:** Bundled in the installer. GStreamer's own MinGW runtime
> (present on the target machine) may depend on it transitively, so it is included
> alongside the other MinGW runtime DLLs. The `flags-mingw` preset omits
> `-fstack-protector-strong` (which would also pull it in) in favour of
> `--dynamicbase`, `--nxcompat`, and `--high-entropy-va` (ASLR + DEP + CFG).

> **NDI runtime:** Not bundled due to licensing. Users must install it separately from
> [ndi.video](https://ndi.video). The NDI 6 Runtime installer is included in the SDK
> download under `Redist/`.

## External build cache

FLTK, rist-cpp, and sdp-tools-cpp build into `build-external/` which is **not** cleaned
when you wipe `build/cross-win64/`. To force a rebuild of an external:

```sh
rm -rf build-external/fltk        # rebuild FLTK
rm -rf build-external/rist-cpp    # rebuild rist + RISTNet
```

The MinGW case-fix symlinks live in `build-external/mingw-case-fix/` and only need to
be created once.
