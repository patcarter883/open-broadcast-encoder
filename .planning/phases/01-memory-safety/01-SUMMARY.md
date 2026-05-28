---
phase: 01-memory-safety
plan: 01
runtime: opencode
assurance: self_checked
---

# Phase 01: Memory Safety & Codec Correctness - Plan 01 Summary

**Completed**: 2026-05-07
**Tasks**: 3
**Git Actions**: None (build succeeds, no commits made)
**Deviations**: 
- `gst_buffer_extract_dup()` API differs from plan specification: GStreamer 1.28 uses `void gst_buffer_extract_dup(GstBuffer*, gsize, gsize, gpointer*, gsize*)` signature (5 args, void return) instead of the plan's assumed 3-arg version that returns `uint8_t*`. Fixed with correct 5-arg signature using `gpointer` destination and `gsize` size output.

**Decisions Made**: 
- Used `gpointer` + `gsize` output parameters for `gst_buffer_extract_dup()` to match GStreamer 1.28 API
- Deregister RIST stats callback in stop() before stopping encoder thread to prevent race condition

**Notes for Verification**: 
- Build succeeds with no errors
- All 5 bugs addressed: BUG-01 (use-after-free via owned buffer), BUG-02 (encoder_ptr reset on stop), BUG-03 (h265parse in all 4 H.265 encoder methods), BUG-04 (nvav1enc in NVENC AV1 encoder), BUG-05 (deque with 1000-entry cap on stats)
- seq and ts_ntp fields removed from buffer_data (were never written or read in codebase)

**Notes for Next Work**: 
- Functional verification requires running the application with actual input (NDI/SDP/MPEGTS) to verify video output is valid
- ASan verification recommended but not possible without running the application
- Phase 2 (Threading Compliance) remains pending

<checks>
<executor_check>
checker: self | same_runtime
checker_runtime: opencode
status: passed
blocking: false
notes: Build succeeds. All code inspection checks pass: buffer_data uses std::vector<uint8_t>, pull_video_buffer/pull_audio_buffer use gst_buffer_extract_dup() with correct GStreamer 1.28 API, transport send_buffer takes const std::vector<uint8_t>&, encoder_ptr reset on stop, H.265 methods use h265parse, NVENC AV1 uses nvav1enc, stats use std::deque with 1000-entry cap.
</executor_check>
</checks>

<handoff>
plan_runtime: opencode
plan_assurance: cross_runtime_checked
plan_check_status: passed
execution_runtime: opencode
execution_assurance: self_checked
executor_check_status: passed
hard_mismatches_open: false
</handoff>

<deltas>
- class: factual_discovery
  impact: recoverable
  disposition: proceeded
  summary: gst_buffer_extract_dup() API differs from plan: GStreamer 1.28 uses 5-arg void-return signature (buffer, offset, size, gpointer*, gsize*) instead of 3-arg uint8_t* return. Fixed with correct signature using gpointer destination.
</deltas>

<judgment>
<active_constraints>
- C++20 required, no C++23 features
- GStreamer 1.28+ API (gst_buffer_extract_dup uses 5-arg signature)
- FLTK requires Fl::lock()/unlock() for cross-thread UI updates
- No changes to UI/FLTK code, NDI code, URL code, or CMake files
- Encoder dispatch switch logic must not be refactored
</active_constraints>
<unresolved_uncertainty>
- Functional verification requires running with actual video input to confirm no corruption
- ASan verification not performed (requires running application)
- nvav1enc availability on target system not confirmed (code change made but encoder must be installed)
</unresolved_uncertainty>
<decision_posture>
Focus on memory safety (owned buffers, pointer lifetime) and codec correctness (right parser/encoder elements). Deferred threading compliance to Phase 2. Deferred functional/runtime testing to post-build verification.
</decision_posture>
<anti_regression>
- H.264 encoding path must continue to work (h264parse unchanged)
- AMD AV1 and QSV AV1 paths must not be affected (av1parse unchanged)
- Software AV1 path must not be affected (rav1enc + av1parse unchanged)
- Audio encoding (aac) path must not be affected
- Existing RIST transport URL construction must not change
- Stats adaptive bitrate algorithm must not change (only container type changed)
- buffer_data.buf_size field kept for backward compatibility
</anti_regression>
</judgment>
