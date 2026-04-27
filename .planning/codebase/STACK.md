# Codebase Stack

**Analysis Date:** 2026-04-27

## Languages

Primary:
- C++20 - used in: `source/`, `include/`, `test/` (all `.cpp`/`.h` files)

Secondary:
- C99 - used in: `external/rist-cpp/` (rist-cpp submodule, CMakeLists.txt line 4)

## Runtime

Environment:
- Linux (Ubuntu 22.04), macOS (macos-14), Windows (Windows Server 2022) per CI matrix in `.github/workflows/ci.yml`

Package manager:
- vcpkg - baseline: `eba7c6a894fce24146af4fdf161fef8e90dd6be3` (pinned in `vcpkg.json` line 22)
- Lockfile: absent (vcpkg uses git SHA baseline, not a JSON lockfile)

## Frameworks And Tooling

Core framework(s):
- GStreamer >=1.28 - purpose: video pipeline construction, encoding, and input handling - subcomponents: gstreamer-1.0, gstreamer-sdp-1.0, gstreamer-rtp-1.0, gstreamer-app-1.0, gstreamer-video-1.0
- FLTK (git submodule) - purpose: cross-platform lightweight GUI for encoder configuration and control - config: `external/fltk/`
- librist (via rist-cpp wrapper, `external/rist-cpp/`) - purpose: RIST protocol for low-latency reliable video transport over IP networks

Testing:
- Catch2 >=3.7.0 - purpose: unit test framework (via vcpkg test feature, `vcpkg.json` line 14) - config: `test/CMakeLists.txt`

Build / dev:
- CMake 3.28+ - purpose: cross-platform build system with multi-generator support (Ninja, Xcode, MSVC) - config: `CMakeLists.txt`, `CMakePresets.json`
- vcpkg - purpose: C++ dependency management for fmt and Catch2
- meson + ninja - purpose: native build of librist within rist-cpp submodule (`external/rist-cpp/CMakeLists.txt` lines 52-73)

Lint / format:
- clang-format - config: `.clang-format` (80-col limit, 2-space indent, left pointer alignment, Chromium-based style)
- clang-tidy - config: `CMakePresets.json` line 41-43 (`clang-tidy` preset)
- cppcheck - config: `CMakePresets.json` line 35-37
- codespell - config: `.codespellrc`, used in CI `ci.yml` line 25-26

## Key Dependencies (Only What Drives Architecture)

Critical libraries:
- **GStreamer** (pkg-config, >=1.28) - why it matters: all video encoding, pipeline construction, and input processing flows through GStreamer elements; the entire encode pipeline lives in `source/encode/encode.cpp` using `gst_parse_launch` and element naming patterns
- **FLTK** (git submodule) - why it matters: single-threaded GUI that must coordinate with background threads (encode thread, transport threads); all UI state lives in `user_interface` class in `source/ui/ui.cpp` with global access from `source/main.cpp`
- **librist** (via rist-cpp wrapper) - why it matters: RIST protocol transport; handles jitter buffering, packet reordering, RTT estimation, and adaptive bitrate stats; custom ExternalProject build pulling `origin/master` from `code.videolan.org/rist/librist.git`
- **fmt >=11.0.2** (vcpkg) - why it matters: `std::format` replacement used extensively for pipeline string construction (`encode.cpp`), log messages (`main.cpp`), and debug output
- **NDI SDK** (external, per-platform) - why it matters: NewTek NDI input for network video capture; custom FindModule at `cmake/modules/FindNDI.cmake` handles platform-specific paths; only fully supported on Windows/Linux, not macOS
- **rist-cpp** (git submodule, `external/rist-cpp/`) - why it matters: C++ wrapper around librist (RISTNet.cpp); manages RIST session lifecycle, log callbacks, stats callbacks; includes LZ4 embedded source (`rist/contrib/lz4/`)

Infra/observability:
- **Doxygen + m.css** - purpose: HTML documentation generation (disabled in CI, `ci.yml` lines 155-201) - config: `cmake/docs-ci.cmake`, `cmake/docs.cmake`
- **lcov / codecov** - purpose: code coverage collection and reporting (disabled in CI, `ci.yml` lines 35-76) - config: `cmake/coverage.cmake`

## Must-Know Packages

Packages where misuse causes hard-to-debug problems:

- **FLTK** — single-threaded GUI framework with background worker threads handling video encoding and transport — risk: high — common mistake: calling UI update methods directly from background threads instead of using `Fl::lock()` / `Fl::unlock()` / `Fl::awake()` pattern documented in AGENTS.md; causes deadlocks or silent UI freezes
- **GStreamer** — pipeline-based video framework with async element state management and callback-based buffer sharing — risk: high — common mistake: not using `gst_bus_timed_pop_filtered` with `GST_CLOCK_TIME_NONE` for error handling, or forgetting `g_main_loop_run()` which causes pipelines to appear to hang without errors
- **librist / rist-cpp** — custom-built (meson) RIST transport with external LZ4, mbedtls, and cjson; handles jitter, reordering, and adaptive bitrate — risk: high — common mistake: confusing RIST profile levels (e.g., unicast vs. interactive) which silently drops packets without GStreamer-level errors; also ristlibrist is fetched from `origin/master` of a forked repo which may introduce breaking changes
- **Catch2** — test discovery via `catch_discover_tests()` with vcpkg-managed dependency — risk: medium — common mistake: tests only run in developer mode (`CMakePresets.json` line 13 sets `open-broadcast-encoder_DEVELOPER_MODE=ON`), so normal builds silently skip all tests without error
- **fmt (libfmt)** — string formatting backend used for GStreamer pipeline construction — risk: medium — common mistake: passing unvalidated user input directly into `std::format` strings used in GStreamer pipeline construction, which produces invalid pipelines that fail at `gst_parse_launch` with cryptic errors

## How To Run

Install:
- Core: `sudo apt install build-essential cmake pkg-config ninja-build clang-format-14 clang-tidy-14 cppcheck doxygen lcov` (Ubuntu)
- vcpkg: clone to `$VCPKG_ROOT`, build with `./bootstrap-vcpkg.sh`, set `VCPKG_ROOT` env var
- GStreamer 1.28+: `sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-good1.0-dev gstreamer1.0-plugins-good`
- NDI SDK: download from NewTek developer portal, set `NDI_SDK_DIR` env var (Linux) or install to `/Library/NDI SDK for Apple` (macOS)
- FLTK + rist-cpp: `git submodule update --init --recursive`

Dev:
- Configure: `cmake --preset=dev` (requires `CMakeUserPresets.json` — see `HACKING.md`)
- Build: `cmake --build --preset=dev`
- Run: `cmake --build build -t run-exe` or `./build/open-broadcast-encoder`

Test:
- `ctest --preset=dev --output-on-failure -j 2`

Build:
- Release: `cmake --preset=ci-ubuntu && cmake --build build --config Release -j 2`
- Sanitize: `cmake --preset=ci-sanitize && ctest --output-on-failure --no-tests=error`

## Configuration

Env:
- How configured: environment variables for external SDK paths (`NDI_SDK_DIR`, `GSTREAMER_1_0_ROOT_MSVC_X86_64`, `VCPKG_ROOT`)
- Key config files: `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `.clang-format`, `.clang-tidy`

CI/CD:
- CI location: `.github/workflows/ci.yml`
- Main checks:
  - `lint`: clang-format check + codespell (Ubuntu 22.04)
  - `coverage`: lcov + codecov upload (disabled by default)
  - `sanitize`: ASAN + UBSAN test run (Ubuntu 22.04)
  - `test`: matrix build + ctest on Ubuntu, macOS, Windows
  - `docs`: Doxygen + m.css HTML generation + GitHub Pages deploy (disabled by default)

---

*Stack analysis: 2026-04-27*
