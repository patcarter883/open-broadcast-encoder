# Codebase Stack

**Analysis Date:** 2026-05-02

## Languages

Primary:
- C++20 - all source files use `cxx_std_20` (CMakeLists.txt:53, CMakePresets.json:52), used in: `source/` (all subdirectories), `include/` (empty), `test/`

Secondary:
- C99 - used by rist-cpp external submodule for librist build (external/rist-cpp/CMakeLists.txt:4)
- Python 3.12 - CI scripts (codespell, m.css docs) (.github/workflows/ci.yml:23, :184)
- CMake 3.28+ - build system (CMakeLists.txt:1)
- Shell/Bash - CI and developer scripts (.github/workflows/ci.yml, cmake/*.cmake)

## Runtime

Environment:
- Native desktop Linux/macOS/Windows (multi-platform via CMake presets)

Package manager:
- vcpkg 2024 (git commit `eba7c6a894fce24146af4fdf161fef8e90dd6be3`) - vcpkg.json:22
- Lockfile: missing - vcpkg.json:1 (no vcpkg.lock present; `builtin-baseline` pins the vcpkg port tree revision)

## Frameworks And Tooling

Core framework(s):
- **FLTK** (git submodule `external/fltk`) - desktop GUI toolkit, builds from source via `add_subdirectory` (CMakeLists.txt:26), UI layer: `source/ui/` (ui.h:7-18 uses FL/Fl*.h headers)
- **GStreamer 1.28+** (system pkg-config) - multimedia pipeline framework, provides video encoding/decoding/demuxing. Required: `gstreamer-1.0`, `gstreamer-sdp-1.0`, `gstreamer-rtp-1.0`, `gstreamer-app-1.0`, `gstreamer-video-1.0` (CMakeLists.txt:32-36). Used in: `source/encode/` (encode.h:13-14), `source/ndi_input/` (ndi_input.h:6-8)
- **rist-cpp** (git submodule `external/rist-cpp`) - RIST protocol C++ wrapper, wraps librist via Meson build (external/rist-cpp/CMakeLists.txt:73), provides transport layer: `source/transport/` (transport.h:9 includes RISTNet.h)
- **NDI (NewTek)** (system package via FindNDI.cmake) - Network Video transport, used in NDI input monitoring: `source/ndi_input/` (ndi_input.cpp uses gst ndisrc element)

Testing:
- **Catch2 3.7.0+** (vcpkg) - unit testing framework, test feature in vcpkg.json:12-19. Test config: `test/CMakeLists.txt:9-10`
- **CTest** (CMake built-in) - test runner invoked via `ctest` (CMakePresets.json:100, ci.yml:66)

Build / dev:
- **CMake 3.28+** - primary build system, uses presets (`CMakePresets.json`), developer mode (`cmake/dev-mode.cmake`), custom module path (`cmake/modules/`)
- **Ninja** (Linux CI generator, CMakePresets.json:86)
- **Xcode** (macOS CI generator, CMakePresets.json:98)
- **Visual Studio 17 2022** (Windows CI generator, CMakePresets.json:106)
- **Meson** - build system for librist (external/rist-cpp/CMakeLists.txt:52, :73)
- **vcpkg** - C/C++ package manager, toolchain file: `${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` (CMakePresets.json:22)
- **cppcheck** - static analysis, invoked via `cppcheck;--inline-suppr` (CMakePresets.json:36)
- **lcov** - code coverage (ci.yml:51)

Lint / format:
- **clang-format 14** - formatting, config: `.clang-format` (80-char limit, 2-space indent, custom brace wrapping)
- **clang-tidy** - static analysis, config: `.clang-tidy` (CMakePresets.json:43), all checks enabled with exclusions
- **codespell** - spell checking, config: `.codespellrc`

CI/CD:
- **GitHub Actions** - `.github/workflows/ci.yml` (jobs: lint, coverage, sanitize, test, docs)
- **CodeCov** - coverage reporting (ci.yml:72, uses `codecov/codecov-action@v4`)
- **GitHub Pages** - docs deployment via `peaceiris/actions-gh-pages@v4` (ci.yml:198)

## Key Dependencies (Only What Drives Architecture)

Critical libraries:
- **GStreamer 1.28+** (pkg-config, `CMakeLists.txt:32-36`) - core video processing pipeline; all encoding, demuxing, and input handling flows through GStreamer elements. Pipeline construction: `source/encode/encode.cpp:53-307`
- **FLTK** (submodule, `CMakeLists.txt:26`) - single GUI thread model; all UI updates from background threads require `Fl::lock()`/`Fl::unlock()` pattern. UI definition: `source/ui/ui.fld` (FLUID form), generated code: `source/ui/ui_func.cxx`
- **rist-cpp / librist** (submodule, `external/rist-cpp/CMakeLists.txt:91-104`) - RIST network transport; builds librist via ExternalProject with Meson. Provides `RISTNetSender` class used in `source/transport/transport.cpp`
- **NDI SDK** (system, `FindNDI.cmake` in `cmake/modules/FindNDI.cmake`) - NDI source discovery and preview; uses GStreamer `ndisrc` element in `source/ndi_input/ndi_input.cpp:64`
- **fmt 11.0.2+** (vcpkg, `vcpkg.json:6-8`) - formatting library; project uses `std::format` (C++20) primarily but fmt is available as vcpkg dependency (likely used transitively by GStreamer or rist)
- **Catch2 3.7.0+** (vcpkg, `vcpkg.json:16-18`) - testing framework; single test file at `test/source/open-broadcast-encoder_test.cpp`
- **homer6/url** (in-tree, `source/url/url.h:1`) - URL parsing for RIST addresses; MIT-licensed, standalone header+source. Used by transport module: `source/transport/transport.cpp:45`

Infra/observability:
- **GStreamer plugins** (system, not listed in vcpkg) - hardware encoder plugins (AMF, QSV, NVENC) required at runtime. Pipeline construction selects elements like `amfh264enc`, `h264_qsv`, `nvenc` (encode.h:52-67)

## Must-Know Packages

Flag 3-5 packages that new contributors must understand before making changes. These are not necessarily the most-used packages - they are the ones where misuse causes hard-to-debug problems.

- **GStreamer** — the entire video pipeline is built from runtime-constructed pipeline strings; a single element name typo or missing plugin causes silent pipeline parse failures. Risk: high — common mistake: building pipeline strings without checking for required GStreamer plugin installation; use `GST_DEBUG=GST_PIPELINE:5` to inspect constructed pipelines at runtime
- **FLTK** — single-threaded UI event loop; all cross-thread UI updates must use `Fl::lock()`/`Fl::unlock()`/`Fl::awake()`. Risk: high — common mistake: calling UI widget methods directly from GStreamer callback threads without locking, causing deadlocks or crashes
- **rist-cpp / librist** — wraps the C librist library via Meson ExternalProject; the rist submodule is built from source via `origin/master` branch (external/rist-cpp/CMakeLists.txt:69), not a tagged release. Risk: medium — common mistake: expecting a stable librist version; the master branch may introduce breaking API changes that break RISTNet.cpp
- **NDI SDK** — system dependency with complex licensing; stub headers at `external/ndi-stub/include/` provide compile-time interface while the runtime SDK is installed separately. Risk: medium — common mistake: assuming NDI works without the full NDI SDK installed on the build host; stub headers allow compilation but linking fails at runtime if `libndi.so` is missing

## How To Run

Install:
- System deps: `pkg-config gstreamer-1.0 gstreamer-sdp-1.0 gstreamer-rtp-1.0 gstreamer-app-1.0 gstreamer-video-1.0 libndi-dev clang-tidy-14 cppcheck` (Ubuntu)
- vcpkg: install from https://github.com/microsoft/vcpkg, set `VCPKG_ROOT` env var
- Submodules: `git submodule update --init --recursive`

Dev:
- Configure: `cmake --preset=dev` (requires `CMakeUserPresets.json` with OS-specific preset)
- Build: `cmake --build --preset=dev`
- Run: `cmake --build --preset=dev -t run-exe`
- Test: `ctest --preset=dev`

Test:
- All platforms: `cmake --preset=ci-linux && cmake --build build -j 16 && ctest --output-on-failure -j 16`
- Sanitizer build: `cmake --preset=ci-sanitize && cmake --build build/sanitize -j 2 && ctest --output-on-failure --no-tests=error -j 2`
- Coverage: `cmake --preset=ci-coverage && cmake --build build/coverage -j 2 && cmake --build build/coverage -t coverage`

Build:
- Release: `cmake -S . -B build -D CMAKE_BUILD_TYPE=Release && cmake --build build`
- Multi-config (MSVC): `cmake -S . -B build && cmake --build build --config Release`
- Install: `cmake --install build --config Release --prefix prefix`

## Configuration

Env:
- How configured: vcpkg via `VCPKG_ROOT` env var, compiler via `CMAKE_CXX_COMPILER`/`CMAKE_C_COMPILER` in presets
- Key config files: `CMakeLists.txt` (main build), `CMakePresets.json` (CI/dev presets), `vcpkg.json` (vcpkg dependencies), `.clang-format` (formatting), `.clang-tidy` (static analysis), `.codespellrc` (spell check)
- `cmake/modules/FindGStreamer.cmake` and `cmake/modules/FindNDI.cmake` — custom CMake find modules

CI/CD:
- CI location: `.github/workflows/ci.yml`
- Main checks: lint (clang-format + codespell on Ubuntu 22.04), sanitize (ASAN/UBSAN on clang++-14), test (matrix: macOS 14, Ubuntu 22.04, Windows 2022), docs (Doxygen + m.css, gated on push to master)

---

*Stack analysis: 2026-05-02*
