# AGENTS.md

<!-- BEGIN GSDD -->
## GSDD Governance (Generated)

Managed by `gsdd`; edit the framework template, not this block.

Lifecycle: `new-project -> plan -> execute -> verify -> audit-milestone`.

Core skills: `gsdd-new-project`, `gsdd-plan`, `gsdd-execute`, `gsdd-verify`, `gsdd-progress`.
Planning state: `.planning/`. Portable workflows: `.agents/skills/gsdd-*/SKILL.md`.

Invoke: `/gsdd-plan` (Claude, OpenCode; Cursor/Copilot/Gemini when skill discovery is available) · `$gsdd-plan` (Codex CLI, plan-only until `$gsdd-execute`) · open SKILL.md directly elsewhere.

Rules:
1. Read before writing roadmap work: `.planning/SPEC.md`, `.planning/ROADMAP.md`, `.planning/config.json`, and the relevant phase plan when one exists.
2. Stay in scope. Implement only what the approved plan or direct user request says. Record unrelated ideas as TODOs.
3. Verify before claiming done: artifact exists, content is substantive, and it is wired into the system.
4. Research unfamiliar domains from real docs and code; never hallucinate paths or APIs.
5. Do not pollute core workflows with vendor-specific syntax; workflow entry lives in `.agents/skills/`, helpers in `.planning/bin/`, and native adapters in their tool-specific directories.
6. Git guidance in `.planning/config.json` -> `gitProtocol` is advisory; follow the repo's own conventions first.

If `.planning/` is missing, run `npx -y gsdd-cli init` then `gsdd-new-project`; bare `gsdd init` is equivalent only when globally installed.
<!-- END GSDD -->

This file provides guidance to agents when working with code in this repository.

C++20 video streaming encoder with FLTK GUI, GStreamer pipelines, and RIST transport.

## Critical Rules

- **Use cmake-skill for all CMake operations** - Do not call cmake via terminal directly
- **Use context7 MCP for GStreamer** - Always query Context7 for GStreamer documentation
- **Use clear-thought MCP** - For code generation/modification and code analysis

## Non-Obvious Patterns

### FLTK Threading (Critical)
All UI updates from background threads MUST use this exact pattern:
```cpp
Fl::lock();        // NOT ui.lock()
// ... update UI ...
Fl::unlock();
Fl::awake();       // Required to wake UI thread
```

### UI Callback Pattern
FLTK callbacks use `FL_METHOD_CALLBACK_N` macros from `FL/fl_callback_macros.H`:
```cpp
FL_METHOD_CALLBACK_2(choice_input_protocol,
                     user_interface, this, choose_input_protocol,
                     input_config*, input_c,
                     FuncPtr, ndi_refresh_funcptr);
```

### Menu Item User Data
Menu items store enum values as `user_data()` for type-safe selection:
```cpp
// In ui.h - cast enum to long, store as void*
{"AMD", 0, 0, (void*)(static_cast<long>(encoder::amd)), ...}

// Retrieval - cast back to enum
auto val = static_cast<encoder>(
    reinterpret_cast<uintptr_t>(choice->mvalue()->user_data()));
```

### Global State Access
Cross-component communication uses globals in [`main.cpp`](source/main.cpp):
```cpp
library app;                          // Shared config/state
std::unique_ptr<transport> transporter;
user_interface ui;
encode* ptr_encoder;                  // For bitrate callback
```

### Include Order
Headers MUST be included in this order:
1. Standard library headers
2. Third-party headers (GStreamer, FLTK)
3. Project headers (lib.h first, then others)

## Build Commands

```bash
# Use cmake-skill (not direct cmake)
cmake --build build -t format-fix    # Must run before commit
cmake --build build -t run-exe       # Run the executable
```

### Developer Mode
Create `CMakeUserPresets.json` (see HACKING.md), then:
```bash
cmake --preset=dev
cmake --build --preset=dev
ctest --preset=dev
```

## Code Style

Enforced by clang-format/clang-tidy (see configs):
- 80 column limit, 2-space indent
- `lower_case` for classes, functions, enums
- Left pointer alignment: `type* ptr`
- Brace wrapping after functions/classes/namespaces
- Include order: system → third-party → project

## Pipeline Building Pattern

The `encode` class builds GStreamer pipelines using string formatting. Elements are named for later retrieval:
```cpp
pipeline_str = std::format("... ! {} name=videncoder ! ...", encoder_element);
video_encoder = gst_bin_get_by_name(GST_BIN(pipeline), "videncoder");
```

### GStreamer Debug Environment Variables
```bash
GST_DEBUG=3                    # Warning level
GST_DEBUG=GST_PIPELINE:5       # Pipeline-specific
GST_DEBUG_DUMP_DOT_DIR=/tmp    # Export DOT graphs
```
