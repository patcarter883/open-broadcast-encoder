# Task 004: Fix Double Pipeline/Bus Cleanup

**Source:** CONCERNS.md Tech Debt #4
**Date:** 2026-05-02

## Problem

`encode::~encode()` (encode.cpp:23-35) and `encode::stop_encode_thread()` (encode.cpp:410-434) both call:
```cpp
gst_element_set_state(this->datasrc_pipeline, GST_STATE_NULL);
gst_object_unref(GST_OBJECT(this->datasrc_pipeline));
gst_object_unref(this->bus);
```

If `stop_encode_thread()` is called before destruction, the destructor double-unrefs already-freed GStreamer objects, causing crashes or heap corruption.

Additionally, `stop_encode_thread()` is currently dead code — never called from the stop flow. The stop button only sets `is_running = false`, but `play_pipeline()` checks `run_flag` (not `is_running`), so the pipeline thread never stops. The destructor then unrefs the bus while the thread is still reading from it.

## Tasks

### Task 1: Add `pipeline_cleaned_up` flag and fix destructor/stop_encode_thread()

**Files:**
- `source/encode/encode.h`
- `source/encode/encode.cpp`

**Action:**
1. Add `bool pipeline_cleaned_up = false;` member to `encode` class in `encode.h` (after `encoder_running` at line 21).
2. In `encode::~encode()` (encode.cpp:23-35):
   - Add null check for `datasrc_pipeline` before GStreamer operations
   - Only unref pipeline/bus if `!pipeline_cleaned_up`
   - Always join threads (add `pipeline_cleaned_up = true;` before thread join block)
3. In `encode::stop_encode_thread()` (encode.cpp:410-434):
   - Set `*run_flag = false;` before other cleanup (so `play_pipeline()` exits its loop)
   - Join all threads before unref'ing GStreamer objects (move thread join from destructor)
   - Perform GStreamer cleanup (set state to NULL, unref pipeline and bus)
   - Set `pipeline_cleaned_up = true;` after cleanup
   - Set `encoder_running = false;` (keep existing)
   - Set `datasrc_pipeline = nullptr; bus = nullptr;` after unref (extra safety)

**Verify:**
```bash
cmake --build build 2>&1 | tail -20
```
Build succeeds with no warnings about unused variables or unreachable code.

**Done:** All code changes compiled. Destructor and `stop_encode_thread()` are both safe to call regardless of order.

### Task 2: Wire `stop_encode_thread()` into main.cpp stop flow

**Files:**
- `source/main.cpp`

**Action:**
Replace the current `stop()` function (main.cpp:83-86):
```cpp
static void stop()
{
  ctx.lib.is_running = false;
}
```
With:
```cpp
static void stop()
{
  ctx.lib.is_running = false;
  if (ctx.lib.encoder_ptr != nullptr) {
    ctx.lib.encoder_ptr->stop_encode_thread();
  }
}
```
This ensures `stop_encode_thread()` is called when the user presses "Stop Encode", which sets `run_flag = false` (stopping `play_pipeline()`), joins the thread, and cleans up GStreamer objects safely.

**Verify:**
```bash
cmake --build build 2>&1 | tail -20
```
Build succeeds.

**Done:** Stop flow calls `stop_encode_thread()` which properly stops the pipeline thread and cleans up before the encoder shared_ptr goes out of scope.
