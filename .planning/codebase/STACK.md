# Codebase Stack

**Analysis Date:** 2026-04-27

<guidelines>
- List versions only when they matter (compatibility, breaking changes, reproducibility).
- Prefer commands and sources of truth (package.json, lockfiles, tool config) over memory.
- Capture the minimum needed for another agent to run, test, and ship changes safely.
- Include concrete commands and file paths.
</guidelines>

## Languages

Primary:
- C++20 - used in: `source/` (all `.cpp`/`.h`/`.hpp` files), `CMakeLists.txt` (lang definitions)

Secondary:
- Shell (Bash/Zsh) - used in: CI workflow scripts (`scripts/` or inline in `.github/workflows/ci.yml`)
- Python 3.12 - used in: CI jobs (`codespell` install, `m.css` doc generation in `.github/workflows/ci.yml`)
- CMake 3.28+ - used in: `CMakeLists.txt`, `CMakePresets.json`, `cmake/` directory

## Runtime

Environment:
- Linux (Ubuntu 22.04, CI default), macOS (macos-14), Windows (Windows Server 2022)

Package manager:
- Vcpkg - version tracked at baseline `eba7c6a894fce24146af4fdf161fef8e90dd6be3` in `vcpkg.json`
- Lockfile: absent (vcpkg uses baseline commit for deterministic builds, not a lockfile)

## Frameworks And Tooling

Core framework(s):
- FLTK (submodule at `external/fltk`) - desktop GUI toolkit for the encoder frontend UI; linked via `fltk::fltk` in `source/ui/CMakeLists.txt`
- rist-cpp (submodule at `external/rist-cpp`) - C++ bindings for RIST protocol; provides `RISTNetSender` API used in `source/transport/rist_transport.cpp` and `source/transport/transport.cpp`
- NewTek NDI SDK (via `find_package(NDI REQUIRED)`) - NewTek Network Device Interface for networked video input; linked as `${NDI_LIBRARIES}` in the main executable target
- GStreamer 1.28+ (via `pkg_search_module` for 5 sub-modules) - media pipeline framework driving encoding, input, and preview pipelines; linked as `PkgConfig::gstreamer*` targets
- fmt >=11.0.2 (via vcpkg) - std::format replacement for compile-time format strings
- homer6/url v0.3.0 (vend`source/url/url.h`) - self-contained RFC 3986 URL parser used in the transport layer

Testing:
- Catch2 >=3.7.0 (via vcpkg `test` feature) - unit testing framework; included via `VCPKG_MANIFEST_FEATURES=test` in dev-mode preset; test subdirectory exists but tests are conditionally added in `cmake/dev-mode.cmake`

Build / dev:
- CMake 3.28 minimum (enforced in `CMakeLists.txt` line 1) - build system with preset-driven configuration for Linux/macOS/Windows
- Ninja generator (Linux CI), Xcode (macOS CI), Visual Studio 17 2022 (Windows CI)

Lint / format:
- clang-format 14 - config: `.clang-format` (80-col limit, 2-space indent, left pointer alignment, C++20 standard, system/3rd-party/project include order)
- clang-tidy - config: `.clang-tidy` (broad check set with project-specific exclusions; lower_case naming for all identifiers)
- cppcheck - static analysis added via `ci-ubuntu` preset (`CMAKE_CXX_CPPCHECK`)
- codespell - spelling checker config: `.codespellrc` (builtin dictionaries: clear, rare, en-GB_to_en-US, names, informal, code)
- doxygen + m.css - documentation generation (disabled by default; requires `BUILD_MCSS_DOCS=ON`)

## Key Dependencies (Only What Drives Architecture)

Critical libraries:
- GStreamer 1.28+ - the entire encoding pipeline is built as a GStreamer pipeline string; `encode` class constructs and manages elements (`pipeline_build_source`, `pipeline_build_video_encoder`, etc.); examples: `source/encode/encode.h`, `source/encode/encode.cpp`, `source/ndi_input/ndi_input.h`, `source/main.cpp` (gst_init)
- rist-cpp (ristnet) - RIST transport protocol implementation; the `transport` class holds a `RISTNetSender*` and uses it for all RIST-based streaming; `rist_transport` adds statistics polling; examples: `source/transport/transport.h`, `source/transport/rist_transport.h`, `source/transport/rist_transport.cpp`
- FLTK - provides the entire GUI; all UI windows, widgets, menus, and callback handling live here; thread-safety requires `Fl::lock()`/`Fl::unlock()`/`Fl::awake()` pattern; examples: `source/ui/ui.h`, `source/ui/ui.cpp`, `source/ui/output_stream_widget.h`, `source/ui/add_output_dialog.h`
- NewTek NDI SDK - networked video input discovery and preview; device monitor runs on a separate thread; examples: `source/ndi_input/ndi_input.h`, `source/ndi_input/ndi_input.cpp`, `source/main.cpp` (ndi object, device monitor)

Infra/observability:
- stats (statistics_aggregator) - collects per-stream metrics (bandwidth, retransmitted packets, quality, RTT) and drives feedback loop to encoder bitrate; examples: `source/stats/stats.h`, `source/stats/statistics_aggregator.h`
- buffer_distributor - shared buffer distribution from single encoder output to multiple transport streams; uses `std::shared_ptr<uint8_t[]>` for zero-copy multi-consumer access; examples: `source/transport/buffer_distributor.h`, `source/transport/buffer_distributor.cpp`

## Must-Know Packages

These are the packages where misuse causes the hardest-to-debug problems:

- GStreamer - the core media pipeline engine; pipeline construction errors silently fail or crash; state management is lifecycle-critical — risk: high — common mistake: not managing element state transitions (PLAYING to NULL) before teardown, or not draining the GMainLoop before cleanup, causing leaks or deadlocks
- FLTK - the GUI layer with non-obvious threading model; all UI updates from background threads must use `Fl::lock()`/`Fl::unlock()`/`Fl::awake()` or the UI deadlocks/freezes — risk: high — common mistake: calling UI setter methods (label, show, hide) directly from a background thread without `Fl::lock()`/`Fl::unlock()`/`Fl::awake()`, causing UI freezes or crashes
- rist-cpp - provides the RIST sender API; callback signatures must match exact signatures expected by the C library; log and statistics callbacks store function pointers — risk: medium — common mistake: passing a lambda with captured variables to a C-style callback, or not setting callbacks before calling `set_up()` on the sender
- fmt - used throughout as `std::format` replacement for `GStreamer` pipeline string construction and log messages; version 11.0.2+ required for format spec changes — risk: low — common mistake: using an older fmt version where `std::format`-compatible signatures differ, causing compile errors
- catch2 - test framework (opt-in via `test` feature); tests run via ctest in CI but test subdirectory is conditionally included — risk: low — common mistake: writing tests that depend on GStreamer pipeline objects without initializing gst first, causing immediate crashes in test runner

## How To Run

Install:
- System deps: `libgstreamer1.0-dev` (>=1.28), `libgstreamer-plugins-base1.0-dev`, `libgstreamer-plugins-bad1.0-dev` (for gstreamer-sdp, gstreamer-rtp, gstreamer-app, gstreamer-video), NDI SDK headers, clang-tidy-14, cppcheck (Linux)
- Vcpkg deps: `vcpkg install fmt catch2` (vcpkg.json specifies versions)
- Submodules: `git submodule update --init --recursive` (fltk, rist-cpp)
- Windows: NDI SDK must be installed manually and discoverable by `find_package(NDI)`; Visual Studio 2022 required

Dev:
- `cmake --preset=dev` (creates `CMakeUserPresets.json`; sets `DEVELOPER_MODE=ON`, `VCPKG_MANIFEST_FEATURES=test`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`)
- `cmake --build --preset=dev`
- `cmake --build build -t format-fix` (run before commit)
- `cmake --build build -t run-exe` (run the executable)

Test:
- `ctest --preset=dev --output-on-failure` (runs all test targets built by dev preset)
- CI runs sanitizers: `cmake --preset=ci-sanitize` then `ctest --output-on-failure` with ASAN + UBSAN

Build:
- `cmake --preset=ci-ubuntu && cmake --build build --config Release -j 2` (Linux CI)
- `cmake --preset=ci-macos && cmake --build build --config Release -j 2` (macOS CI)
- `cmake --preset=ci-windows && cmake --build build --config Release -j 2` (Windows CI)
- Install: `cmake --install build --config Release --prefix prefix`

## Configuration

Env:
- How configured: CMake cache variables, environment variables for Vcpkg (`VCPKG_ROOT`, `VCPKG_TARGET_TRIPLET`), CMake presets for cross-platform config
- Key config files: `CMakeLists.txt` (main build), `CMakePresets.json` (multi-platform presets), `vcpkg.json` (vcpkg dependencies + baseline), `.clang-format` (code style), `.clang-tidy` (linting rules), `.codespellrc` (spelling)

CI/CD:
- CI location: `.github/workflows/ci.yml`
- Main checks:
  - `lint` (Ubuntu 22.04): clang-format 14 check + codespell
  - `test` (matrix: macos-14, ubuntu-22.04, windows-2022): configure + build + install + ctest
  - `sanitize` (Ubuntu 22.04, clang++-14): ASAN + UBSAN build + test
  - `coverage` (Ubuntu 22.04): lcov coverage report → codecov (disabled by default)
  - `docs` (Ubuntu 22.04): doxygen + m.css → GitHub Pages (disabled by default)
- Vcpkg baseline locked to commit `eba7c6a894fce24146af4fdf161fef8e90dd6be3`

---

*Stack analysis: 2026-04-27*
