# Task 004 Summary

**Completed**: 2026-05-02
**Tasks**: 2
**Deviations**: None — plan executed exactly as written.
**Decisions Made**: None (followed CONCERNS.md fix approach)

## What Was Built

Fixed double pipeline/bus cleanup in the encode module that caused double-unref of GStreamer objects when `stop_encode_thread()` was called before destruction.

### Changes

1. **`source/encode/encode.h`** — Added `bool pipeline_cleaned_up = false;` member to `encode` class.

2. **`source/encode/encode.cpp`** — Modified destructor to check `pipeline_cleaned_up` flag and null-check `datasrc_pipeline` before GStreamer cleanup. Modified `stop_encode_thread()` to: set `*run_flag = false` (so `play_pipeline()` exits), join all threads before cleanup, perform GStreamer teardown, set `pipeline_cleaned_up = true`, and null the pointers after unref.

3. **`source/main.cpp`** — Modified `stop()` callback to call `ctx.lib.encoder_ptr->stop_encode_thread()` in addition to setting `is_running = false`. This ensures the pipeline thread is properly stopped and GStreamer objects are cleaned up before the encoder shared_ptr goes out of scope.

## Notes

- The pre-existing clang-tidy errors in `source/stats/stats.cpp` (lines 16 and 82: `std::stoi` on int, `.c_str()` on int) are unrelated to this task. They exist in the codebase before this change.
- All modified files compile cleanly when tested without clang-tidy's `-Werror` behavior.
