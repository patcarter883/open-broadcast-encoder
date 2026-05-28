---
id: 011
title: Fix CMake build errors
status: planned
priority: 0
scope:
  - CRITICAL-1: name-shadowing in lib.h (GCC 15 hard error)
  - CRITICAL-2: FLTK X11 link deps missing
  - HIGH-4: NDI CMake variable name mismatch
  - MEDIUM-5: vcpkg dead fmt dependency
  - LOW: lint patterns search *.hpp not *.h
  - LOW: CMakePresets cmakeMinimumRequired mismatch
skip:
  - Item 3 (ndisrc GStreamer plugin - system package)
  - transport.h raw new / u_int16_t (non-blocking code style)
created: 2026-05-03
---

# Plan: Fix CMake Build Errors

## Task 1: Fix name-shadowing in lib.h

GCC 15 treats `-Wchanges-meaning` as a hard error. Five members in
`source/lib/lib.h` shadow their type names, preventing compilation.

### Renames

| Struct | Line | Old member | New member | Rationale |
|--------|------|-----------|------------|-----------|
| `encode_config` | 67 | `codec` | `selected_codec` | matches `selected_input` / `selected_input_mode` pattern |
| `encode_config` | 68 | `encoder` | `selected_encoder` | consistent with above |
| `library` | 121 | `input_config` | `input_cfg` | `_cfg` avoids clash with type; `_c` already used for const refs |
| `library` | 122 | `encode_config` | `encode_cfg` | same |
| `library` | 123 | `output_config` | `output_cfg` | same |

### Files

| File | What changes |
|------|-------------|
| `source/lib/lib.h` | Rename 5 member declarations (lines 67, 68, 121, 122, 123) |
| `source/main.cpp` | Update `ctx.lib.input_config` → `ctx.lib.input_cfg` (lines 66, 144, 154, 209, 211), `ctx.lib.encode_config` → `ctx.lib.encode_cfg` (lines 55, 67, 212), `ctx.lib.output_config` → `ctx.lib.output_cfg` (lines 99, 213) |
| `source/encode/encode.cpp` | Update `encode_c.codec` → `encode_c.selected_codec` (lines 168, 185, 202, 219, 345), `encode_c.encoder` → `encode_c.selected_encoder` (line 147) |
| `source/ui/ui.cpp` | Update `encode_config->codec` → `encode_config->selected_codec` (line 602), `encode_config->encoder` → `encode_config->selected_encoder` (line 610) |

### Action

1. Edit `source/lib/lib.h` — rename the 5 members
2. Edit `source/main.cpp` — update 10 references (5 `input_config`, 3 `encode_config`, 2 `output_config`)
3. Edit `source/encode/encode.cpp` — update 6 references (5 `.codec`, 1 `.encoder`)
4. Edit `source/ui/ui.cpp` — update 2 references (1 `->codec`, 1 `->encoder`)

### Verify

```bash
cmake --build build 2>&1 | head -50
```

No `-Wchanges-meaning` errors. All references compile.

### Done

- [ ] All 5 member renames applied in lib.h
- [ ] All 18 references updated across main.cpp, encode.cpp, ui.cpp
- [ ] Build succeeds without name-shadowing errors

---

## Task 2: Fix CMake build configuration

Five separate CMake/config issues that prevent linking or cause
misconfiguration.

### Sub-issues

#### 2a. FLTK X11 link deps (CRITICAL-2)

`cmake/ExternalBuilds.cmake` line 48-59: `fltk::fltk` IMPORTED STATIC
target has no `INTERFACE_LINK_LIBRARIES`. Linker produces 169+ undefined
references for X11, Xcursor, Xfixes, Xinerama, Xft, fontconfig, dl,
pthread, m.

Fix: Add `INTERFACE_LINK_LIBRARIES` to the `set_target_properties` call
after line 58:

```cmake
set_target_properties(fltk::fltk PROPERTIES
  IMPORTED_LOCATION "${_fltk_lib_location}"
  INTERFACE_INCLUDE_DIRECTORIES "${FLTK_INSTALL_DIR}/include"
  INTERFACE_LINK_LIBRARIES "X11;Xcursor;Xfixes;Xinerama;Xft;fontconfig;dl;Threads::Threads;m"
)
```

`Threads::Threads` is already found at line 17 of the same file.

#### 2b. NDI variable name mismatch (HIGH-4)

`cmake/modules/FindNDI.cmake` sets `NDI_LIBS` and `NDI_INCLUDE_DIR`, but
`CMakeLists.txt` consumes `NDI_LIBRARIES` (line 61) and
`NDI_INCLUDE_DIRS` (line 68). Variables are empty → link/include failures.

Fix: In `FindNDI.cmake`, rename:
- `NDI_LIBS` → `NDI_LIBRARIES` (lines 14, 25, 45, 52)
- `NDI_INCLUDE_DIR` → `NDI_INCLUDE_DIRS` (lines 11, 22, 42, 49)

Also fix the `find_package_handle_standard_args` call (line 59) to use
`NDI_LIBRARIES` and `NDI_INCLUDE_DIRS` instead of `NDI_DIR`.

#### 2c. Remove dead fmt dependency from vcpkg.json (MEDIUM-5)

`vcpkg.json` declares `fmt` but code uses `std::format` exclusively.
`fmt` is never imported or linked. Dead dependency causes vcpkg
resolution overhead and potential VCPKG_ROOT failures.

Fix: Remove the fmt entry from `vcpkg.json` dependencies array.

#### 2d. Lint patterns search *.hpp not *.h (LOW)

`cmake/lint-targets.cmake` line 3: `source/*.hpp` should be
`source/*.h`. Line 4: `include/*.hpp` — no `include/` directory exists.
Line 5: `test/*.hpp` should be `test/*.h`.

Fix: Change all `*.hpp` to `*.h` in `FORMAT_PATTERNS` (line 3). Remove
`include/*.hpp` line (line 4) since the directory doesn't exist.

#### 2e. CMakePresets cmakeMinimumRequired mismatch (LOW)

`CMakePresets.json` lines 3-7: `cmakeMinimumRequired` is 3.14.0 but
`CMakeLists.txt` requires 3.28.

Fix: Change `"minor": 14` to `"minor": 28` on line 5.

### Files

| File | Sub-issues |
|------|-----------|
| `cmake/ExternalBuilds.cmake` | 2a |
| `cmake/modules/FindNDI.cmake` | 2b |
| `vcpkg.json` | 2c |
| `cmake/lint-targets.cmake` | 2d |
| `CMakePresets.json` | 2e |

### Action

1. Edit `cmake/ExternalBuilds.cmake` — add `INTERFACE_LINK_LIBRARIES` to `fltk::fltk`
2. Edit `cmake/modules/FindNDI.cmake` — rename `NDI_LIBS` → `NDI_LIBRARIES`, `NDI_INCLUDE_DIR` → `NDI_INCLUDE_DIRS`; fix `find_package_handle_standard_args`
3. Edit `vcpkg.json` — remove fmt from dependencies
4. Edit `cmake/lint-targets.cmake` — change `*.hpp` → `*.h`, remove `include/*.hpp` line
5. Edit `CMakePresets.json` — update `cmakeMinimumRequired.minor` from 14 to 28

### Verify

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -20
cmake --build build 2>&1 | tail -30
```

No undefined-reference errors for FLTK/X11 symbols. NDI include dirs
resolve. `cmake --build build -t format-check` runs against `*.h` files.
`CMakePresets.json` minimum version is 3.28.

### Done

- [ ] FLTK target has INTERFACE_LINK_LIBRARIES for X11 stack
- [ ] FindNDI.cmake exports NDI_LIBRARIES and NDI_INCLUDE_DIRS
- [ ] fmt removed from vcpkg.json
- [ ] Lint patterns use *.h not *.hpp
- [ ] CMakePresets cmakeMinimumRequired is 3.28.0
- [ ] Full build succeeds
