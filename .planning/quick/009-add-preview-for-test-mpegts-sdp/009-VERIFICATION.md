# Quick Task 009 Verification

## Task Description
Add preview input support for test source, mpegts, and sdp.

## Verification Checklist

### Requirement Coverage
- [x] Test source preview: Uses videotestsrc pattern=smptebars + autovideosink
- [x] MPEGTS preview: Uses udpsrc with configurable port + tsdemux + autovideosink
- [x] SDP preview: Uses sdpsrc with SDP file + rtpvrawdepay + autovideosink
- [x] NDI preview: Preserved existing functionality (ctx.ndi->preview())
- [x] None mode: No-op (unchanged)

### Task Completeness
- [x] `run_preview_pipeline()` helper implemented and tested
- [x] `preview_input()` expanded with all 4 input mode cases
- [x] NDI preview hang bug fixed (removed g_main_loop_run)
- [x] Build succeeds without errors
- [x] Code formatted with clang-format

### Dependency Correctness
- [x] Includes added for gst/video/video.h, fstream, filesystem
- [x] No new dependencies introduced
- [x] Uses existing GStreamer elements already available on system

### Scope Sanity
- [x] No changes to existing encode pipeline logic
- [x] No changes to UI widget definitions
- [x] No changes to config structs
- [x] Only 2 files modified

### Must Have Quality
- [x] Preview pipeline properly cleans up resources (bus, pipeline unref)
- [x] Error messages logged through encode_log callback
- [x] SDP preview uses temp file (avoids inline SDP string issues)
- [x] NDI preview fix preserves existing thread-based execution

## Result: passed
