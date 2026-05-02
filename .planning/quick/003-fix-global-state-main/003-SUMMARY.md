# Task 003 - Fix Global Mutable State in main.cpp

**Completed**: 2026-05-02
**Tasks**: 3
**Git Actions**: none (not requested)
**Deviations**: None — plan executed exactly as written.
**Decisions Made**:
- `app_context` struct holds all global state: `library`, `user_interface*`, `unique_ptr<transport>`, `unique_ptr<ndi_input>`
- `library` struct extended with `shared_ptr<encode> encoder_ptr` and `shared_ptr<std::atomic<bool>> run_flag`
- `user_interface` kept as stack-local in `main()` with pointer stored in context (FLTK singleton requirement)

## Summary

Replaced 5 global mutable objects (`library app`, `std::unique_ptr<transport>`, `user_interface ui`, `encode* ptr_encoder`, `ndi_input ndi`) with a single `app_context` struct. All static callback closures in `main.cpp` now access state through the context object instead of bare globals.

### Critical Bug Fixes (Stage 1)
- **Dangling `ptr_encoder`**: Replaced raw `encode*` with `std::shared_ptr<encode>` stored in `library` struct. Added null-check guard in `rist_stats_cb`.
- **Init order**: `ndi_input` now constructed in `main()` after `ui.init_ui()`, eliminating undefined initialization order.
- **Run flag type mismatch**: Added `shared_ptr<std::atomic<bool>> run_flag` to `library` to satisfy `encode` constructor signature.

### Architectural Consolidation (Stage 2-3)
- Introduced `app_context` struct in `lib.h` bundling all previously-global state
- Forward declarations for `transport`, `ndi_input`, `user_interface` avoid circular includes
- All 5 globals reduced to 1 (`app_context ctx`)

## Files Modified
- `source/lib/lib.h` — Added forward declarations, `app_context` struct, `encoder_ptr` and `run_flag` members to `library`
- `source/lib/lib.cpp` — Initialize `run_flag` in `library` constructor
- `source/main.cpp` — Replaced 5 globals with `app_context`; updated all callback closures; fixed init order; added null checks

## Pre-existing Issues (Not Fixed)
- `stats.cpp:11` — `std::stoi(encode_config.bitrate)` where `bitrate` is `int`, not `std::string`. Pre-existing bug.
- `stats.cpp:68` — `encode_config.bitrate.c_str()` where `bitrate` is `int`. Pre-existing bug.
- These were present in the original codebase before this task.
