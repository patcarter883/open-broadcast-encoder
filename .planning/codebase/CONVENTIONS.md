# Codebase Conventions

**Analysis Date:** 2026-04-27

<guidelines>
- This document is durable intent (rules/patterns), not a directory dump.
- Be prescriptive. If 80%+ of the codebase follows a pattern, document it as a rule ("Use X"), not as a survey ("Sometimes X").
- Every non-trivial claim must include at least one concrete file path example.
- Prefer "how to do it here" over general best practices.
- Capture testing and mocking boundaries explicitly ("what NOT to mock"). Missing boundaries cause broken tests and slow CI.
- Capture external integration patterns (webhook verification, auth session management). Missing these causes security vulnerabilities.
</guidelines>

## Naming Patterns

**Files:**
- Use `snake_case` for all source files and directories: `ndi_input.h`, `statistics_aggregator.cpp`, `output_stream_widget.cpp`, `transport_manager.h`
- Header guards: always use `#pragma once` — all 17 headers use this pattern exclusively
- CMake subdirectories match file names: `source/encode/CMakeLists.txt` builds `encode.cpp` and `encode.h`

**Classes:**
- `lower_case` for class names: `encode`, `transport`, `transport_base`, `transport_manager`, `user_interface`, `ndi_input`, `statistics_aggregator`, `buffer_distributor`
- Use `class` (not `struct`) for types with behavior/methods: `class encode`, `class user_interface`
- Use `struct` for plain data aggregates with no behavior: `struct buffer_data`, `struct shared_buffer`, `struct input_config`, `struct encode_config`, `struct stream_config`, `struct cumulative_stats`, `struct stream_stats`

**Enums:**
- `lower_case` for enum types and all enum constants:
  ```cpp
  enum class input_mode : std::uint8_t { mpegts, sdp, ndi, test, none };
  enum class codec : std::uint8_t { h264, h265, av1 };
  enum class encoder : std::uint8_t { amd, qsv, nvenc, software };
  enum class transport_protocol : std::uint8_t { rist, srt, rtmp };
  ```
- Always use scoped enums (`enum class`) with explicit underlying type (`std::uint8_t`)

**Functions and Methods:**
- `lower_case` for all functions: `run_encode_thread()`, `build_pipeline()`, `set_encode_bitrate()`, `pull_video_buffer()`
- Prefix pipeline-building methods with `pipeline_build_`: `pipeline_build_source()`, `pipeline_build_video_encoder()`, `pipeline_build_audio_encoder()`
- Use trailing-return-type syntax for function declarations: `auto pull_video_buffer() -> buffer_data;`

**Variables:**
- `lower_case` for local variables: `auto vidbuf = encoder.pull_video_buffer();`, `auto transport = transport_manager.create_transport(config);`
- Use `this->` explicitly when accessing member variables (dominant pattern, seen in 8 source files)

**Template Parameters:**
- `CamelCase` for template type parameters: configured in `.clang-tidy` for `TypeTemplateParameterCase`, `TemplateParameterCase`, `ValueTemplateParameterCase`, `TemplateTemplateParameterCase`

**Private/Protected Members:**
- Prefix with `m_` for class members: `m_transport_manager`, `m_stats_callback`, `m_log_callback`, `m_config`, `m_stats`, `m_output_widgets`

## Code Style

**Formatting:**
- Tool: `clang-format` (configured in `.clang-format`)
- Key settings:
  - `ColumnLimit: 80`
  - `IndentWidth: 2`
  - `PointerAlignment: Left`
  - `BreakBeforeBraces: Custom` (K&R-style, but with specific wrapping rules)
  - `BreakConstructorInitializers: BeforeComma`
  - `NamespaceIndentation: None`
  - `SpaceBeforeCtorInitializerColon: true`
  - `SortIncludes: true` (alphabetical)
  - `IncludeBlocks: Regroup`
- Must run `cmake --build build -t format-fix` before commit

**Linting:**
- Tool: `clang-tidy` (configured in `.clang-tidy`)
- Enabled: all checks minus exclusions (`google-readability-todo`, `altera-*`, `fuchsia-*`, `llvm-header-guard`, `llvm-include-order`, `llvmlibc-*`, `modernize-use-nodiscard`, `misc-non-private-member-variables-in-classes`)
- Key enforcement:
  - `cppcoreguidelines-narrowing-conversions.PedanticMode: true`
  - `readability-simplify-boolean-expr.ChainedConditionalReturn: true`
  - `readability-else-after-return.WarnOnUnfixable: true`
  - `readability-qualified-auto.AddConstToQualified: true`
  - `bugprone-misplaced-widening-cast.CheckImplicitCasts: true`
- Must run `cmake --build build -t format-fix` (which includes clang-tidy) before commit

**Spelling:**
- Tool: `codespell` (configured in `.codespellrc`)
- Builtin dictionaries: `clear, rare, en-GB_to_en-US, names, informal, code`
- Skip paths: `*/.git`, `*/build`, `*/prefix`

**Build System:**
- CMake 3.28+ with C++20 standard (`cxx_std_20`)
- Multi-config CI presets in `CMakePresets.json`:
  - `ci-ubuntu`: clang-tidy + cppcheck + dev-mode
  - `ci-sanitize`: AddressSanitizer + UndefinedBehaviorSanitizer
  - `ci-coverage`: gcov coverage
  - `ci-linux`/`ci-darwin`/`ci-win64`: platform-specific compilers and flags
- `vcpkg` for dependency management
- Developer mode requires `CMakeUserPresets.json` (see `HACKING.md`)

## Import Organization

**Order (strict):** See `.clang-format` IncludeCategories and `AGENTS.md`):**

1. Standard library headers (`<...>`) — lowest priority, sorted alphabetically
2. C++ standard library headers with extensions (`<*.h(pp)>`)
3. Third-party framework headers (`<Gst...>`, `<FL/...>`, `<RIST...>`)
4. Project headers (`"..."`) — highest priority, includes must be sorted

**Project header rule:** Always include `lib/lib.h` first among project headers, then other project headers. Example from `source/ndi_input/ndi_input.cpp`:

```cpp
#include "ndi_input/ndi_input.h"      // Self-include first
#include <format>
#include <thread>
#include <chrono>
#include <gst/gst.h>
// ... third-party GStreamer headers
```

Example from `source/main.cpp`:

```cpp
#include <condition_variable>         // 1. Standard library
#include <gst/gst.h>                  // 2. Third-party GStreamer
#include "stats.h"                    // 3. Project: lib.h equivalent (core headers first)
#include "encode.h"
#include "transport.h"
// ... more project headers
```

**Using directives:**
- Rarely used, only where needed for brevity: `using std::string;` appears in 3 files (`encode.cpp`, `transport.cpp`, `rist_transport.cpp`) — prefer fully-qualified `std::string` in new code

## Error Handling

**GStreamer errors:**
- Use `GError*` passed by reference (`GError* err = nullptr;`), then `g_clear_error(&err)` after use
- Pattern: parse with `gst_message_parse_error(msg, &err, &debug_info);` → log `err->message` → `g_clear_error(&err)` → `g_free(debug_info)`
- Files following this pattern: `source/main.cpp` (line 153-167), `source/encode/encode.cpp` (line 448-458), `source/ndi_input/ndi_input.cpp` (line 92-105)

**C++ exceptions:**
- Use `try`/`catch (const std::exception& e)` in the encoder FFI wrapper layer (`source/encoder_wrapper/encoder_wrapper.cpp`) and the lib layer (`source/lib/lib.cpp`)
- Catches are generic — do not attempt to distinguish exception types
- This pattern appears in the Tauri C FFI wrapper: `source/encoder_wrapper/encoder_wrapper.cpp` uses this in every exported function (12 try/catch blocks)

**Pipeline/GST errors:**
- GStreamer pipeline errors are handled via the message bus, not exceptions
- Implement `handle_gst_message_error()` and `handle_gst_message_eos()` methods for structured error handling
- Always check element pointers before use: `if (this->datasrc_pipeline == nullptr)`

## Module / Function Design

**Pipeline building:**
- The `encode` class builds GStreamer pipelines through a stepwise builder pattern: `build_pipeline()` → `parse_pipeline()` → `play_pipeline()`
- Each pipeline element type gets its own builder method: `pipeline_build_source()`, `pipeline_build_video_encoder()`, `pipeline_build_amd_h264_encoder()`, etc.
- Elements are named for later retrieval: `gst_bin_get_by_name(GST_BIN(pipeline), "videncoder")`
- Pipeline strings use `std::format()` for dynamic parameterization

**Callback architecture:**
- Logging uses function pointers as callbacks: `using log_func_ptr = void (*)(const std::string& msg);` (declared in `encode.h`, `ndi_input.h`)
- Stats use `std::function` callbacks: `using stats_callback = std::function<void(const stream_stats& stats)>;` (declared in `transport_base.h`)
- Transport send completion uses callbacks: `using send_callback = std::function<void(bool success, const std::string& error)>;` (declared in `transport_base.h`)

**Transport factory pattern:**
- `transport_manager` (in `source/transport/transport_manager.h`) acts as a factory returning `std::shared_ptr<transport_base>`
- Concrete types: `rist_transport`, `srt_transport`, `rtmp_transport` (all inherit from `transport_base`)
- `transport_base` is an abstract base with pure virtual `initialize()`, `start()`, `stop()`, `send_buffer()`, `is_connected()`, `get_stream_id()`, `get_protocol()`, `get_stats()`

**Buffer distribution:**
- Legacy buffers (`buffer_data`) are converted to shared buffers (`shared_buffer`) via `buffer_distributor`
- `shared_buffer` uses `std::shared_ptr<uint8_t[]>` with a no-op deleter to avoid freeing externally-owned data
- `buffer_distributor` distributes to multiple transports simultaneously

## Convention Adoption Rates

- **lower_case naming for all identifiers (classes, functions, enums, variables):** `~100% (stable)` — enforced by clang-tidy `readability-identifier-naming` checks; 0 violations observed across all 17 source files
- **`#pragma once` header guards:** `~100% (stable)` — all 17 header files use `#pragma once`; no traditional include guards observed
- **scoped enums with explicit underlying type (`enum class T : std::uint8_t`):** `~100% (stable)` — all 4 enums in `source/lib/lib.h` follow this pattern exactly
- **`this->` explicit member access:** `~53% (stable)` — 8 of 15 source files use `this->` consistently (mostly `encode.cpp` with 45+ occurrences); newer files in the transport layer (`transport_manager.cpp`, `buffer_distributor.cpp`, `statistics_aggregator.cpp`) largely omit `this->`
- **`std::format` for string construction:** `~53% (stable)` — 8 of 15 source files use `std::format`; used throughout `encode.cpp` (17 occurrences), `transport.cpp`, `ndi_input.cpp`, `main.cpp`, but absent from `transport_base.cpp`, `lib.cpp`, `url.cc`, and several newer modules
- **structs for data-only types, classes for behavior:** `~89% (stable)` — 8 of 9 data aggregates are `struct` with zero methods; `library` is a `class` because it has a constructor and a `log_append()` method
- **callback-based logging (function pointer or std::function):** `~72% (stable)` — 11 of 15 source files use function-pointer or std::function callbacks for logging/stats; main.cpp passes callbacks via pointers; `transport_base` uses std::function; legacy `transport` class uses C function pointers
- **C++ exceptions in business logic:** `~0% (stable)` — exceptions are only caught in the FFI wrapper layer (`encoder_wrapper.cpp`) and `lib.cpp`; no production code throws or requires try/catch in core modules
- **`using std::string;` at namespace scope:** `~20% (stable)` — appears in only 3 files (`encode.cpp`, `transport.cpp`, `rist_transport.cpp`); new files should avoid this pattern

## Testing And Mocking (High-Leverage)

**Test types used:**
- Unit: yes (runner: Catch2) — example: `test/source/open-broadcast-encoder_test.cpp`
- Integration: no detected test files
- E2E: no detected test files

**Where tests live:**
- Unit tests: `test/source/*.cpp` — single test file in the project currently

**Fixtures and factories:**
- Where fixtures live: not detected — the single test file instantiates `library {}` directly
- Preferred factory pattern: not established — test coverage is minimal

**Mocking boundaries (explicit):**
- Do mock: external services and libraries not easily available in test environments (e.g., GStreamer pipelines, NDI device discovery)
- Do NOT mock: `library` struct (trivial to instantiate, used directly in the single existing test), configuration structs (`input_config`, `encode_config`, `output_config`) — these are POD-like and better initialized inline
- Why: the codebase is heavily GStreamer-driven; full pipeline tests require a running GStreamer environment. The single existing test (`open-broadcast-encoder_test.cpp`) exercises only `library {}` construction and the `name` field, requiring no mocks
- Example: `test/source/open-broadcast-encoder_test.cpp` creates `auto const lib = library {};` directly — no factory, no mock, no fixture

**CI reliability rules:**
- Test discovery: CMake uses `Catch2` with CTest; macOS preset uses `PRE_TEST` discovery mode
- No standardized timeouts, flake policy, or parallelism constraints documented in CMakePresets.json
- Developer mode (CMakePresets.json) enables: `dev-mode` activates clang-tidy, cppcheck, and test targets

**What to test when adding new code:**
- New transport implementations: test `transport_base` interface implementation via factory
- Configuration structs: test default values and initialization
- Pipeline building: integration-level test (requires GStreamer environment); may need to mock `gst_parse_launch`
- Buffer distribution: test with synthetic `buffer_data` inputs and verify `shared_buffer` properties

## External Integration Patterns (Security-Critical)

### GStreamer Pipeline Injection
- Pipelines are constructed from `std::format` strings and passed to `gst_parse_launch()`
- Do NOT concatenate unvalidated user input into pipeline strings (injection risk)
- Input validation must occur before pipeline string construction (see `source/encode/encode.cpp` line 53-75 where `input_c.selected_input` is formatted into pipeline strings)

### NDI Device Discovery
- `ndi_input` uses GStreamer device monitoring (`GstDeviceMonitor`) to discover NDI sources
- Device names are user-facing strings; sanitize before inclusion in UI or log output
- See `source/ndi_input/ndi_input.cpp` lines 38-55 (`refresh_devices()`)

### Transport Configuration
- Addresses and stream keys come from user input via UI widgets
- `stream_config` passes raw strings to transport constructors; validate address format before construction
- RTMP transport receives `stream_key` as plain string — ensure it is not logged or exposed in debug output
- See `source/transport/rtmp_transport.h` line 8, `source/transport/rtmp_transport.cpp`

### RIST Logging Callbacks
- RIST library callbacks (`rist_log_cb`, `rist_stats_cb`) are registered in `main.cpp` (lines 46-62)
- Callbacks pass through to UI log append — ensure re-entrancy safety when UI is locked

### FLTK UI Thread Safety
- All FLTK UI updates from background threads MUST use `Fl::lock()` / `Fl::unlock()` followed by `Fl::awake()`
- The `user_interface` class declares `lock()` and `unlock()` methods (see `source/ui/ui.h` lines 90-91)
- Do NOT call FLTK widget methods directly from background threads

## Golden Files

These production files best exemplify the codebase's conventions, selected by highest density of documented patterns:

- `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cpp` — demonstrates `lower_case` naming, `this->` member access, `std::format` usage, `std::uint8_t` scoped enums, GStreamer error handling (`GError*`), pipeline builder pattern, trailing-return-type declarations, `#pragma once`, and `#include` ordering (self-include → standard → third-party → project)
- `/home/pat/Projects/open-broadcast-encoder/source/transport/transport_base.h` — demonstrates `#pragma once`, `lower_case` class name, `m_` prefixed members, `std::function` callbacks, abstract base class with pure virtual methods, explicit `= delete` / `= default` for copy/move semantics, structured documentation comments, and `using` type alias declarations
- `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.h` — demonstrates `#pragma once`, `enum class T : std::uint8_t` pattern, `struct` for data aggregates with default member initializers, `#include` ordering (standard library first), and self-contained type definitions used across the codebase

---

*Convention analysis: 2026-04-27*
