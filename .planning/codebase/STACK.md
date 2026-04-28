# Codebase Stack

**Analysis Date:** 2026-04-28

## Languages

Primary:
- C++ [20] - all source files use `.cppm` extension for C++20 modules; entry point `source/main.cpp`; modules defined in `source/*/` with `export module` directives

Secondary:
- CMake [3.28+ required] - build system, all project configuration lives in `CMakeLists.txt` and `cmake/*.cmake`

## Runtime

Environment:
- Linux (primary target), macOS, Windows (MSVC) - CI matrix in `.github/workflows/ci.yml` covers `macos-14`, `ubuntu-22.04`, `windows-2022`

Package manager:
- vcpkg - `[vcpkg.json]` with `builtin-baseline: "eba7c6a894fce24146af4fdf161fef8e90dd6be3"`
- pkg-config - GStreamer sub-components resolved via `pkg_search_module` in `CMakeLists.txt:32-36`
- Lockfile: missing - vcpkg uses `builtin-baseline` in `vcpkg.json` rather than a separate lockfile

## Frameworks And Tooling

Core framework(s):
- GStreamer 1.28+ - media pipeline framework; drives all encoding, demuxing, and transport logic; found in `source/encode/encode.cppm` with 578 lines of pipeline construction; sub-components: `gstreamer-1.0`, `gstreamer-sdp-1.0`, `gstreamer-rtp-1.0`, `gstreamer-app-1.0`, `gstreamer-video-1.0` (all via `pkg_search_module` in `CMakeLists.txt:32-36`)
- FLTK (external submodule) - GUI toolkit; embedded at `external/fltk`; used exclusively in `source/ui/ui.cppm` for the 1373x667 main window with input/select/encode/stats/log panels
- rist-cpp (external submodule) - C++ wrapper around librist for RIST protocol; embedded at `external/rist-cpp`; used in `source/transport/transport.cppm` to send encoded video over RIST network

Testing:
- Catch2 3.7+ - testing framework declared in `vcpkg.json` feature `"test"`; test targets in `test/CMakeLists.txt` (currently commented out in `cmake/dev-mode.cmake:4-6`)
- ctest - CMake test runner used in CI (`ci.yml:153`)

Build / dev:
- CMake 3.28+ - build system; `CMakeLists.txt` at project root; developer presets in `CMakePresets.json` with 18+ named presets (ci-linux, ci-macos, ci-windows, ci-sanitize, ci-coverage, clang-tidy, cppcheck)
- Ninja generator - default for Linux CI (`CMakePresets.json:86`)
- Clang 14 - primary compiler on Linux CI (`ci.yml:82`)
- clang-tidy - static analysis in `ci-clang-tidy` preset (`CMakePresets.json:40-44`)
- cppcheck - static analysis in `ci-cppcheck` preset (`CMakePresets.json:33-37`)
- lcov + codecov - code coverage pipeline (`ci.yml:36-75`)
- Address/UndefinedBehaviorSanitizer - sanitizer build preset (`ci.yml:77-108`)
- codespell - spell checking via `cmake/spell.cmake` (`ci.yml:32-33`)
- clang-format 14 - lint tool (`ci.yml:29`); configuration at `.clang-format`; column limit 80, indent width 2, pointer alignment left

## Key Dependencies (Only What Drives Architecture)

Critical libraries:
- GStreamer >= 1.28 - core media framework; every pipeline (input demux, encoding, output mux) is built as a GStreamer element graph; found in `source/encode/encode.cppm` with 15 encoder variants (amd/qsv/nvenc/software x h264/h265/av1); all pipeline construction uses `gst_parse_launch()` (`encode.cppm:430`)
- rist-cpp (submodule at `external/rist-cpp`) - wraps librist for RIST Advanced profile transport; provides `RISTNetSender` class; used in `source/transport/transport.cppm` to send video buffers over network with adaptive bitrate support; statistics callback wired at `main.cpp:50-55`
- fmt >= 11.0.2 - string formatting library via vcpkg; used extensively with `std::format` across all modules for pipeline string construction, URL formatting, and log messages
- NDI SDK (system dependency, `find_package(NDI REQUIRED)` in `CMakeLists.txt:29`) - enables NDI input source; device discovery via `GstDeviceMonitor` in `source/ndi_input/ndi_input.cppm`; pipeline uses `ndisrc` and `ndisrcdemux` elements
- homer6/url (header-only at `source/url/url.h`) - RFC 3986-compliant URL parser; used by `source/transport/transport.cppm` to parse RIST destination URLs; v0.3.0 MIT licensed
- sdp-tools-cpp (submodule at `external/sdp-tools-cpp`) - SDP parsing tools; included in `CMakeLists.txt:25` but currently commented out; used for SDP/RTP input mode

Infra/observability:
- CI/CD: GitHub Actions (`.github/workflows/ci.yml`) - 5 jobs: lint, coverage, sanitize, test (3-platform matrix), docs; codecov integration for coverage reporting
- vcpkg baseline pin (`vcpkg.json:22`) - reproducible dependency versions locked to git commit `eba7c6a894fce24146af4fdf161fef8e90dd6be3`

## Must-Know Packages

- GStreamer - why critical: the entire encoding pipeline is built as a dynamic GStreamer element graph; pipeline strings are assembled via `std::format` and parsed at runtime with `gst_parse_launch()`; a malformed pipeline string produces silent parse failures - risk: high - common mistake: forgetting that `gst_bin_get_by_name()` returns `nullptr` if the element was never created by `gst_parse_launch()`, leading to null pointer dereferences at `encode.cppm:439-444`
- rist-cpp / librist - why critical: manages the RIST network transport with adaptive bitrate; statistics callbacks drive real-time bitrate adjustment; callback misuse causes silent data loss - risk: high - common mistake: forgetting that RIST PROFILE_ADVANCED (`transport.cppm:109`) changes protocol behavior vs the default profile; also, `rist_sender->sendData()` at `transport.cppm:117` takes a raw pointer with no ownership transfer, so buffer lifetime must be guaranteed
- FLTK - why critical: all UI updates from background threads must use `Fl::lock()`/`Fl::unlock()`/`Fl::awake()` pattern; without proper locking, the GUI freezes or crashes - risk: medium - common mistake: calling UI widget methods directly from the stats callback thread (`stats.cppm:67-86`) without acquiring `Fl::lock()` first, or calling `Fl::unlock()` without `Fl::awake()` to wake the main thread event loop
- CMake (developer presets) - why critical: developer mode must be explicitly enabled via `CMakeUserPresets.json` or `open-broadcast-encoder_DEVELOPER_MODE` cache variable; test targets and dev tools (clang-tidy, cppcheck, format, coverage) are only available in dev mode - risk: low - common mistake: running `cmake --build build` without configuring via a dev preset first, resulting in missing `format-fix` and `coverage` targets
- vcpkg (builtin-baseline) - why critical: dependency versions are pinned to a specific vcpkg git commit; `vcpkg.json:22` uses `builtin-baseline` which replaces the traditional lockfile mechanism - risk: low - common mistake: expecting a `vcpkg.json.lock` file; vcpkg resolves versions from the baseline commit on each configure, so reproducible builds depend on network access to the vcpkg registry at baseline time

## How To Run

Install:
- `git submodule update --init --recursive` - initializes FLTK, rist-cpp submodules
- Install system deps: GStreamer 1.28+ dev packages (gstreamer-1.0, gstreamer-sdp-1.0, gstreamer-rtp-1.0, gstreamer-app-1.0, gstreamer-video-1.0), NDI SDK, clang-14 (Linux)
- Install vcpkg at a location pointed to by `VCPKG_ROOT` environment variable

Dev:
- Configure: `cmake --preset=dev` (requires user-created `CMakeUserPresets.json` inheriting from `dev-mode`, `vcpkg`, `ci-linux`)
- Build: `cmake --build --preset=dev`
- Format check: `cmake --build build -t format-check`
- Format fix: `cmake --build build -t format-fix`

Test:
- Configure with dev mode (enables test features via `VCPKG_MANIFEST_FEATURES=test`)
- Build tests: `cmake --build build`
- Run: `ctest --preset=dev` (requires `test/` CMakeLists.txt uncommented in `cmake/dev-mode.cmake:4-6`)

Build:
- Release: `cmake -S . -B build -D CMAKE_BUILD_TYPE=Release && cmake --build build`
- Run exe: `cmake --build build -t run-exe` (target defined in `cmake/dev-mode.cmake:9-13`, currently commented out)

## Configuration

Env:
- How configured: CMake cache variables, environment variables (`VCPKG_ROOT`), CMakePresets.json
- Key config files: `CMakePresets.json` (18 presets for CI/dev), `vcpkg.json` (dependencies), `.clang-format` (code style), `.clang-tidy` (static analysis)
- No runtime config file - all configuration happens through UI widget inputs (IP address, port, codec selection, encoder selection, bitrate)

CI/CD:
- CI location: `.github/workflows/ci.yml`
- Main checks: lint (clang-format + codespell), test (3-platform matrix: macos-14, ubuntu-22.04, windows-2022), sanitize (ASAN+UBSAN on ubuntu), coverage (Ubuntu with lcov+codecov), docs (Doxygen+m.css deploy to gh-pages)
- Lint job runs on all PRs and pushes to `master`; other jobs gated on lint success; coverage and docs require `github.repository_owner` match (disabled in fork)

---

*Stack analysis: 2026-04-28*
