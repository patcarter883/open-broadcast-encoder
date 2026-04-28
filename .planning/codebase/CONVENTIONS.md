# Codebase Conventions

**Analysis Date:** 2026-04-28

## Naming Patterns

**Files:**
- Source files use `.cppm` extension for C++20 module interface units and `.cpp` for implementation units. Headers use `.h`.
- All source files live in flat subdirectories under `source/`: `source/encode/`, `source/transport/`, `source/ui/`, `source/lib/`, `source/stats/`, `source/ndi_input/`, `source/url/`.
- Each subdirectory contains a matching set: `<name>.h`, `<name>.cppm`, `<name>.cpp`, and a `CMakeLists.txt`.
- Subdirectory names use lowercase `snake_case`.

**Functions:**
- Use `snake_case` for all functions and methods.
  - `pipeline_build_source()` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:107`
  - `handle_gst_message_error()` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:500`
  - `run_encode_thread()` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:465`
  - `got_rist_statistics()` → `/home/pat/Projects/open-broadcast-encoder/source/stats/stats.cppm:17`
- Pipeline builder methods follow the pattern `pipeline_build_<component>_<variant>`:
  - `pipeline_build_amd_h264_encoder()` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:283`
  - `pipeline_build_qsv_av1_encoder()` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:329`

**Variables:**
- Use `snake_case` for all local variables, parameters, and class members.
  - `pipeline_str` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:40`
  - `run_flag` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:36`
  - `log_func` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:37`
  - `input_c`, `encode_c` — abbreviations for config references → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:38-39`
- Do NOT use `m_` prefix for private members despite clang-tidy configuration (`/home/pat/Projects/open-broadcast-encoder/.clang-tidy:116`). The codebase consistently omits the prefix.

**Types:**
- **Classes**: all lowercase (no CamelCase, no PascalCase).
  - `encode` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:19`
  - `transport` → `/home/pat/Projects/open-broadcast-encoder/source/transport/transport.cppm:17`
  - `user_interface` → `/home/pat/Projects/open-broadcast-encoder/source/ui/ui.cppm:27`
  - `stats` → `/home/pat/Projects/open-broadcast-encoder/source/stats/stats.cppm:10`
- **Structs**: `snake_case`.
  - `input_config` → `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:57`
  - `encode_config` → `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:62`
  - `output_config` → `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:68`
  - `buffer_data` → `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:35`
- **Enums (enum class)**: `snake_case` names and values, backed by `std::uint8_t`.
  - `enum class input_mode : std::uint8_t` → `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:12`
  - `enum class codec : std::uint8_t` → `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:20`
  - `enum class encoder : std::uint8_t` → `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:27`
- **Type aliases**: `snake_case`.
  - `using FuncPtr = void (*)();` → `/home/pat/Projects/open-broadcast-encoder/source/ui/ui.cppm:25`
  - `using log_func_ptr = void (*)(const std::string& msg);` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:17`

**Module names:**
- Module names are short, lowercase identifiers matching the subdirectory.
  - `export module library;` → `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:9`
  - `export module encode;` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:14`
  - `export module transport;` → `/home/pat/Projects/open-broadcast-encoder/source/transport/transport.cppm:11`
  - `export module ui;` → `/home/pat/Projects/open-broadcast-encoder/source/ui/ui.cppm:22`
  - `export module stats;` → `/home/pat/Projects/open-broadcast-encoder/source/stats/stats.cppm:7`
  - `export module ndi_input;` → `/home/pat/Projects/open-broadcast-encoder/source/ndi_input/ndi_input.cppm:11`

## Code Style

**Formatting:**
- **Tool:** clang-format (invoked via `cmake --build build -t format-fix`).
- **Config:** `/home/pat/Projects/open-broadcast-encoder/.clang-format`
- **Key settings:**
  - `ColumnLimit: 80` — break lines at 80 characters
  - `IndentWidth: 2` — two-space indentation
  - `UseTab: Never` — spaces only
  - `PointerAlignment: Left` — `type* var` not `type *var`
  - `BreakBeforeBraces: Custom` — K&R-style braces with custom wrapping
  - `BraceWrapping.AfterFunction: true` — function bodies get a newline before `{`
  - `BraceWrapping.AfterClass: true` — class bodies get a newline before `{`
  - `BraceWrapping.AfterNamespace: true`
  - `AlwaysBreakBeforeMultilineStrings: true` — multi-line strings break before opening
  - `IncludeBlocks: Regroup` — grouped include blocks
  - `IncludeCategories: [Standard library <...>, <...h>, <...>, everything else]` — ordered imports
  - `SortIncludes: true` — includes sorted alphabetically
  - `SpacesBeforeTrailingComments: 2`
  - `SpacesInEmptyBlock: false` — no space in empty braces
  - `SpaceInEmptyParentheses: false` — no space in empty parens
- **Usage:** Always run `cmake --build build -t format-fix` before committing. The CI `ci-ubuntu` preset runs `clang-tidy` and `cppcheck` on every build.

**Linting:**
- **clang-tidy:** `/home/pat/Projects/open-broadcast-encoder/.clang-tidy`
  - Enables nearly all checks with targeted exclusions: `-google-readability-todo`, `-fuchsia-*`, `-llvmlibc-*`, `-modernize-use-nodiscard`, `-misc-non-private-member-variables-in-classes`
  - Strict mode enabled for: `bugprone-misplaced-widening-cast`, `bugprone-sizeof-expression`, `cppcoreguidelines-narrowing-conversions`
  - **Custom naming rules override all identifier styles to `lower_case`** for classes, functions, variables, members, methods, enums — including `ClassCase: lower_case`, `MethodCase: lower_case`, `MemberCase: lower_case`, `FunctionCase: lower_case`
  - Private members: `PrivateMemberPrefix: m_` is configured but not used in practice.
  - Template parameters: `CamelCase` (the only exception to the lower_case rule).
- **codespell:** `/home/pat/Projects/open-broadcast-encoder/.codespellrc`
  - Builtin dictionaries: `clear,rare,en-GB_to_en-US,names,informal,code`
  - Skips: `.git`, `build`, `prefix` directories
  - Targets: `spell-check` and `spell-fix` CMake targets
- **cppcheck:** Integrated via `ci-ubuntu` preset with `--inline-suppr` flag.

**C++ Standard:**
- C++20 is the enforced standard: `cxx_std_20` on all targets.
- CI preset enforces: `CMAKE_CXX_STANDARD: "20"`, `CMAKE_CXX_STANDARD_REQUIRED: ON`, `CMAKE_CXX_EXTENSIONS: OFF`.

## Import Organization

**C++20 Module imports:**
- Every `.cppm` file starts with a global module fragment: `module;`
- Then the export declaration: `export module <name>;`
- Then `import library;` (the shared types module)
  - `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:15`
  - `/home/pat/Projects/open-broadcast-encoder/source/transport/transport.cppm:12`
  - `/home/pat/Projects/open-broadcast-encoder/source/ui/ui.cppm:23`
  - `/home/pat/Projects/open-broadcast-encoder/source/ndi_input/ndi_input.cppm:12`
  - `/home/pat/Projects/open-broadcast-encoder/source/stats/stats.cppm:8`
- Secondary imports follow module dependency: `import ui;` in stats → `/home/pat/Projects/open-broadcast-encoder/source/stats/stats.cppm:9`

**Non-module headers:**
- Legacy `.h` headers use `#pragma once` (all 8 headers).
- Non-module utility: `/home/pat/Projects/open-broadcast-encoder/source/url/url.h` in namespace `homer6`.

**Include order in `.cppm` files:**
1. Global module fragment (`module;`) with standard library `#includes`
2. System/external headers (`#include <gst/...>`)
3. Module export (`export module ...;`)
4. Module imports (`import ...;`)
5. Type declarations and definitions

**Legacy comment-out:** `// #include "common.h"` appears in all 6 `.cppm` files — the pre-module shared types header is superseded by the `library` module.

## Error Handling

**GStreamer errors:**
- Use GError pattern: declare `GError* error = nullptr`, pass to GStreamer API, check `error != nullptr`, log via `g_clear_error(&error)`.
  - `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:426` — `parse_pipeline()`
- Bus message error handler: `handle_gst_message_error()` parses error and debug info, logs both, frees resources.
  - `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:500`
- Bus message EOS handler: `handle_gst_message_eos()` sets `encoder_running = false`.
  - `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:516`

**Constructor exceptions:**
- The `library` constructor wraps initialization in `try {} catch (const std::exception& e) { std::terminate(); }`.
  - `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:105`

## Logging

**Framework:** Function pointer callbacks — `log_func_ptr = void (*)(const std::string& msg)`.
- Defined in `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:17`, `/home/pat/Projects/open-broadcast-encoder/source/ndi_input/ndi_input.cppm:13`
- Passed to constructor, stored as member, invoked via `log(msg)` method.
  - `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:573`
- Callbacks route to UI display: `ui.encode_log_append(msg)` and `ui.transport_log_append(msg)` via lambdas in `/home/pat/Projects/open-broadcast-encoder/source/main.cpp:23-31`.

**RIST transport logging:**
- Uses C-style callback: `int (*)(void*, enum rist_log_level, const char*)` bound via `set_log_callback()`.
  - `/home/pat/Projects/open-broadcast-encoder/source/transport/transport.cppm:45`
- Implemented in `/home/pat/Projects/open-broadcast-encoder/source/main.cpp:42-48` as `rist_log_cb`.

**Pattern:** Every component that produces log output uses the function pointer callback pattern — never direct I/O or std::cout.

## Module / Function Design

**Function size:**
- Pipeline builder methods are small and focused (~5-15 lines each), each constructing a fragment of the GStreamer pipeline string.
  - `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:283-289` — `pipeline_build_amd_h264_encoder`
- The main orchestration method `build_pipeline()` sequences all builders (~10 lines).
  - `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:411`
- Bus message handler dispatches to specialized handlers (~10 lines).
  - `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:522`

**Parameters:**
- Config structs (`input_config`, `encode_config`, `output_config`) passed by `const&` to constructors.
  - `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:28-31`
- Function pointer callbacks passed to constructors or setter methods.
- Callbacks store abbreviated references: `const input_config& input_c;` → `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:38`

**Return values:**
- Use trailing return type syntax: `auto pull_video_buffer() -> buffer_data;`
  - `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm:25`
- Non-void returns are consistent: `buffer_data` for video/audio buffers, `bool` for statistics result, `void` otherwise.

**Exports:**
- All public types in `lib.cppm` are `export`ed: 3 enums + 5 structs + 1 class (`library`).
  - `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:12-79`
- Other modules export their main class/struct: `encode`, `transport`, `user_interface`, `stats`, `ndi_input`.
- `library` class methods are **not** exported from the module interface — they are declared but the module export only covers types, not member functions of the `library` struct (except via the `.h` header).

## Convention Adoption Rates

- **`snake_case` for all identifiers (functions, variables, types, enums):** `~100% (stable)` — enforced by both clang-tidy config (`.clang-tidy` sets all naming rules to `lower_case`) and codified by 100% of identifiers observed in 6 production module files.
- **C++20 modules (`.cppm` interface files):** `~100% (stable)` — all 6 source modules have `.cppm` interface units starting with `module;` global fragment.
- **`std::format` for string construction:** `~95% (stable)` — 46 uses across encode, transport, ndi_input modules; no `printf` or `std::to_string` for general formatting found in pipeline construction.
- **Switch statements on enums for dispatch:** `~100% (stable)` — 18 switch statements on `input_mode`, `encoder`, and `codec` enums; zero if-else chains replacing them.
- **Function pointer callback for logging:** `~100% (stable)` — 16 references to `log_func_ptr` pattern across encode and ndi_input modules; zero direct I/O.
- **FLTK thread locking (`Fl::lock`/`Fl::unlock`):** `~100% (stable)` — 40 lock/unlock pairs in UI code; every cross-thread UI update uses the lock pattern.
- **Trailing return type syntax (`-> Type`):** `~85% (stable)` — 22 uses found in encode, ndi_input, stats modules; not used for all functions (constructor methods use traditional syntax).
- **`std::atomic` for shared state:** `~60% (stable)` — 14 uses across lib and encode modules; `is_running` and `encoder_running` are atomic, but `transporter` uses `std::unique_ptr` without atomic (potential race).
- **`std::unique_ptr` / `std::make_unique`:** `~5% (low)` — only 2 instances in `/home/pat/Projects/open-broadcast-encoder/source/main.cpp:19,87`. Raw pointers used elsewhere (GStreamer elements, `transport` class member).
- **Testing (Catch2 tests):** `~5% (declining)` — 1 test case in `/home/pat/Projects/open-broadcast-encoder/test/source/open-broadcast-encoder_test.cpp`; test subdirectory is commented out in `cmake/dev-mode.cmake:4-6`.
- **`#pragma once` include guards:** `~100% (stable)` — all 8 `.h` headers use `#pragma once`.

## Testing And Mocking (High-Leverage)

**Test types used:**
- **Unit:** Yes — Catch2 v3 framework (`find_package(Catch2 REQUIRED)` in `/home/pat/Projects/open-broadcast-encoder/test/CMakeLists.txt:9`).
- **Integration:** No tests detected.
- **E2E:** No tests detected.

**Where tests live:**
- `/home/pat/Projects/open-broadcast-encoder/test/source/open-broadcast-encoder_test.cpp`

**Fixtures and factories:**
- None detected. The single test directly instantiates `library{}`.

**Mocking boundaries (explicit):**
- **Do mock:** External library interfaces — GStreamer pipeline elements, NDI device enumeration (`ndisrc`), RIST transport layer (`RISTNetSender`). These are platform-dependent and unavailable in CI.
- **Do mock:** UI components (`Fl_Double_Window`, `Fl_Choice`) — FLTK requires a display server and is non-deterministic.
- **Do NOT mock:** `library` struct — it is the central state container with simple data members; mocking it would duplicate the real structure.
  - Example: `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:79`
- **Do NOT mock:** `encode_config`, `output_config` — these are plain data structs with no behavior; mocking them adds indirection without benefit.
  - Example: `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm:62-77`
- **Do NOT mock:** The adaptive bitrate algorithm in `stats::got_rist_statistics()` — this is pure arithmetic logic that should be tested with real `rist_stats` data.
  - Example: `/home/pat/Projects/open-broadcast-encoder/source/stats/stats.cppm:17`

**Network calls in tests:**
- Blocked by default — RIST transport and NDI require hardware/network dependencies. Mock the callback interface (`log_func_ptr`, `rist_stats_callback`) instead of the real transport.

**CI reliability rules:**
- Test discovery via `catch_discover_tests()` in `/home/pat/Projects/open-broadcast-encoder/test/CMakeLists.txt:22`.
- Test subdirectory is currently commented out in `/home/pat/Projects/open-broadcast-encoder/cmake/dev-mode.cmake:4-6` — tests must be re-enabled before CI will run them.

## External Integration Patterns (Security-Critical)

This codebase integrates with GStreamer media pipelines, NDI network devices, and RIST network transport. It does not use HTTP webhooks, JWT authentication, or external API keys.

### Network Transport Configuration
- RIST URLs are constructed programmatically from user-provided `output_config.address` and parameters.
  - `/home/pat/Projects/open-broadcast-encoder/source/transport/transport.cppm:92-103`
- NDI device discovery uses GStreamer's `gst_device_monitor` with `Video/Source` caps filter.
  - `/home/pat/Projects/open-broadcast-encoder/source/ndi_input/ndi_input.cppm:52`
- No input validation on address strings — raw strings are passed directly to GStreamer pipeline and RIST URL constructors.
- Do NOT add unvalidated user input to pipeline strings — this is an injection vector (GStreamer pipeline injection).

### Environment Configuration
- All configuration comes from UI widget values bound to `input_config`, `encode_config`, `output_config` structs.
- No config files or environment variables are read at runtime.
- GStreamer init: `gst_init(&argc, &argv)` → `/home/pat/Projects/open-broadcast-encoder/source/main.cpp:115`
- Do NOT hardcode RIST addresses or credentials — they are user-input from FLTK widgets.

### Observability Hooks
- Transport log display: `Fl_Text_Display` widget at `/home/pat/Projects/open-broadcast-encoder/source/ui/ui.cppm:68`.
- Encode log display: `Fl_Text_Display` widget at `/home/pat/Projects/open-broadcast-encoder/source/ui/ui.cppm:69`.
- Both log buffers are `Fl_Text_Buffer` instances, updated via `log_func_ptr` callbacks.
- No health check endpoint or external monitoring.

## Golden Files

These files demonstrate the highest density of documented conventions:

- `/home/pat/Projects/open-broadcast-encoder/source/encode/encode.cppm` — C++20 module interface (`module;` + `export module encode;`), 15 pipeline builder methods using `pipeline_build_<component>_<variant>` naming pattern, switch dispatch on `encoder` and `codec` enums, `std::format` for pipeline string construction, trailing return types (`-> buffer_data`), function pointer logging callback (`log_func_ptr`), GStreamer error handling (`GError*` pattern), `explicit` constructor, all-`snake_case` identifiers, 2-space indentation.

- `/home/pat/Projects/open-broadcast-encoder/source/lib/lib.cppm` — Central types module exporting 3 `enum class` types (`input_mode`, `codec`, `encoder`), 5 `struct` types (`buffer_data`, `cumulative_stats`, `input_config`, `encode_config`, `output_config`), and 1 `struct` class (`library`). All use `lower_case` naming, `std::uint8_t` backing for enums, `noexcept` constructor with `try/catch { std::terminate() }`, `std::atomic_bool` for shared state flag. Demonstrates the module import pattern (`import library;` used by all 5 other modules).

- `/home/pat/Projects/open-broadcast-encoder/source/stats/stats.cppm` — Pure algorithmic logic (adaptive bitrate adjustment) with no external dependencies beyond the logging callback and UI update. Demonstrates the `ui.lock()`/`ui.unlock()` FLTK threading pattern, `std::accumulate` with online average algorithm, trailing return type syntax (`-> bool`), and the callback-driven UI update pattern. Minimal surface area makes it ideal for unit testing.

- `/home/pat/Projects/open-broadcast-encoder/source/main.cpp` — Orchestrator file demonstrating the global state pattern (`library app`, `std::unique_ptr<transport>`, `user_interface ui`, `encode* ptr_encoder`), static callback lambdas (`encode_log`, `transport_log`, `rist_log_cb`, `rist_stats_cb`), `std::bind_front` for method callbacks, and the FLTK callback macro wiring via `init_ui_callbacks()`. Shows how all modules interconnect.

---

*Convention analysis: 2026-04-28*
