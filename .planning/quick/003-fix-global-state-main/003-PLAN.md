# Task 003 - Fix Global Mutable State in main.cpp

## Objective

Replace the 5 global mutable objects in `main.cpp` (lines 18-33) with a single `app_context` object. The fix is staged in two phases: (1) critical bug fixes — dangling `ptr_encoder` and initialization order; (2) architectural refactor — consolidate into a context struct.

## Critical Issues

- `encode* ptr_encoder` at line 21 is a dangling pointer: set to `&encoder` where `encoder` is stack-local in `run_loop()` (line 63), but `rist_stats_cb` dereferences it (line 53) after the stack frame may unwind
- `ndi` at line 33 is constructed with `&encode_log` before `ui.init_ui()` is called (line 118), creating undefined initialization order
- All 5 globals (`app`, `transporter`, `ui`, `ptr_encoder`, `ndi`) are tightly coupled, making testing impossible and component lifecycle ambiguous

## Approach

### Stage 1: Critical Fixes (Task 1)
- Replace raw `encode* ptr_encoder` with `std::shared_ptr<encode> ptr_encoder` stored in the `library` struct
- Add null-check guard in `rist_stats_cb` before dereferencing
- Move `ndi` construction into `main()` after `ui.init_ui()` so `encode_log` callback is safe

### Stage 2: Architectural Context (Task 2-3)
- Introduce `app_context` struct in `lib.h` that bundles all globals: config structs, stats, transport pointer, encoder shared_ptr, ndi instance, callbacks
- Replace `main.cpp` globals with a single `app_context app_ctx` object
- Update callback closures to capture `app_ctx` reference instead of accessing globals

## Files Modified

| File | Change |
|------|--------|
| `source/lib/lib.h` | Add `shared_ptr<encode> encoder_ptr` member to `library` struct; add `app_context` struct |
| `source/lib/lib.cpp` | Update `library()` constructor to initialize encoder_ptr |
| `source/main.cpp` | Replace 5 globals with `app_context`; fix init order; add null-check in `rist_stats_cb` |
| `source/encode/encode.cpp` | Minor: ensure encoder lifecycle compatible with shared_ptr |

## Task 1: Fix dangling ptr_encoder + init order

**Action:**
- Add `std::shared_ptr<encode> encoder_ptr` member to `library` struct in `lib.h` (after `is_running`, before config structs)
- In `run_loop()`: change `encode* ptr_encoder;` global to remove; set `app.encoder_ptr = std::make_shared<encode>(...)` instead of raw pointer
- In `rist_stats_cb`: add null-check `if (!app.encoder_ptr) return;` before `set_encode_bitrate` call
- Move `ndi` construction from global scope into `main()` function body, after `ui.init_ui()` call
- Remove global `encode* ptr_encoder` declaration from `main.cpp`

**Verify:**
```bash
cmake --build build 2>&1 | head -50
grep -n 'ptr_encoder' source/main.cpp
grep -n 'encoder_ptr' source/lib/lib.h
```
Expected: No bare `encode*` globals; `encoder_ptr` present in library struct; `rist_stats_cb` has null-check; ndi not at file-scope in main.cpp.

## Task 2: Introduce app_context struct

**Action:**
- Create `app_context` struct in `lib.h` that bundles: `input_config`, `encode_config`, `output_config`, `cumulative_stats`, `std::shared_ptr<encode> encoder_ptr`, `std::atomic_bool is_running`, `std::vector<std::thread> threads`
- Remove config fields from `library` struct (or keep `library` as thin wrapper)
- In `main.cpp`: replace `library app;` with `app_context ctx;`
- Update all references to `app.input_config`, `app.encode_config`, `app.output_config`, `app.stats`, `app.is_running` to use `ctx`

**Verify:**
```bash
cmake --build build 2>&1 | head -80
grep -c 'library app' source/main.cpp  # should be 0
grep -c 'app\.' source/main.cpp         # should be 0
```

## Task 3: Consolidate remaining globals into context

**Action:**
- Add `std::shared_ptr<transport> transporter` to `app_context`
- Add `std::unique_ptr<ndi_input> ndi` to `app_context`
- Move callback closures (`encode_log`, `transport_log`, `rist_log_cb`, `rist_stats_cb`) to capture context reference
- In `run()`: store encoder in context, use `ctx.transporter->send_buffer(...)`
- In `run_transport()`: use `ctx.transporter = std::make_unique<transport>()`
- Remove remaining globals: `transporter`, `ui`, `ndi` from `main.cpp`
- Keep `user_interface` as a non-global: pass it through context or constructor injection
- The `ui` instance can remain as a named global since FLTK requires a single instance, but all other state moves to context

**Verify:**
```bash
cmake --build build 2>&1 | head -80
grep -E '^\s*(library|std::unique_ptr<transport>|encode\*|ndi_input)\s' source/main.cpp  # should match nothing
grep -c 'ctx\.' source/main.cpp  # should be high (many references)
```
Expected: Only `user_interface ui` remains as a global (FLTK singleton requirement). Everything else in `app_context`.
