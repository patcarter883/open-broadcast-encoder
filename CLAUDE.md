# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & run

CMake ≥ 3.28, C++20. NDI SDK and GStreamer ≥ 1.28 must be installed on the host; vcpkg manages Catch2.

Quick build:
```sh
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build
```

Use a CI preset for stricter checks (lint + clang-tidy + cppcheck + dev-mode):
```sh
cmake --preset=ci-ubuntu      # or ci-macos / ci-windows
cmake --build build -j 2
```

Sanitizer build (required when verifying memory-safety fixes — see SPEC):
```sh
cmake --preset=ci-sanitize
cmake --build build/sanitize
ctest --test-dir build/sanitize
```

Lint / spellcheck:
```sh
cmake -D FORMAT_COMMAND=clang-format-14 -P cmake/lint.cmake
cmake -P cmake/spell.cmake
```

External deps (FLTK, rist-cpp, sdp-tools-cpp) build via ExternalProject into `/build-external/`. Cleaning `/build/` does **not** rebuild them — wipe `/build-external/` to force a rebuild.

## Architecture

Data flow: **NDI / testsrc / mpegts / sdp input → GStreamer encode pipeline → RIST transport → network**.

Modules under `source/`:

- `main.cpp` — entry point. Initializes GStreamer, builds the FLTK UI, wires callbacks, and drives the encode loop (`encoder->pull_video_buffer()` → `transporter->send_buffer()`).
- `lib/` — shared types and global state. `library` holds configs, encoder ptr, stats, and thread flags. `buffer_data` owns its bytes (`vector<uint8_t>`) — never store raw pointers into unmapped GStreamer memory. `cumulative_stats` uses bounded deques (cap ~1000) to avoid unbounded growth.
- `encode/` — GStreamer pipeline construction. Dynamically composes input source + parser + encoder based on `input_mode`, `codec`, and `encoder` enums. **Pipeline elements must match the codec** (`h264parse`/`h265parse`/`av1parse`; `nvav1enc` for NVENC AV1 — not `x264enc`). Supports AMD/QSV/NVENC/software × H.264/H.265/AV1.
- `ndi_input/` — NDI device discovery via a background monitor thread; launches a preview pipeline.
- `transport/` — RIST sender wrapping `RISTNetSender`. Holds `rist_log_cb` and `rist_stats_cb` callbacks. The stats callback fires from a RIST-internal thread and can outlive the encoder — use `weak_ptr` for any encoder reference inside it.
- `stats/` — aggregates RIST stats into the bounded deques for UI display (bandwidth, bitrate, packet loss).
- `ui/` — FLTK GUI. Background threads (NDI monitor, RIST stats) must wrap UI mutations in `Fl::lock()` / `Fl::unlock()`.
- `url/` — URL parsing helper.

## Project conventions

See `.planning/SPEC.md` and `.planning/ROADMAP.md` for the active milestone. Current focus (Phase 1): **memory safety and codec correctness**. Recently fixed bug classes that future work must not regress:

1. **Buffer ownership** — `buffer_data` owns bytes; do not return pointers into GStreamer-mapped memory that may be unmapped before the consumer reads it.
2. **Callback lifetime** — RIST stats callback can fire after encoder teardown; capture `weak_ptr`, never `shared_ptr` or raw `this`.
3. **GStreamer element matching** — codec selection must propagate to parser and encoder element names; mismatches silently corrupt output.
4. **Bounded stats** — stats deques are capped (~1000 entries, ~70s @ 14Hz). Do not reintroduce unbounded containers.
5. **FLTK thread safety** — every UI mutation from a non-main thread is wrapped in `Fl::lock()` / `Fl::unlock()`.

RAII is mandatory. AddressSanitizer runs in CI (`ci-sanitize`); fixes for memory bugs must include a sanitizer-clean run as evidence. Phase 2 (deferred) will add Helgrind validation for threading.

## CI

`.github/workflows/ci.yml` runs lint, coverage (lcov), sanitize (ASan), and build matrix (ubuntu/macos/windows) on push to `master`. vcpkg commit is pinned in the workflow.
