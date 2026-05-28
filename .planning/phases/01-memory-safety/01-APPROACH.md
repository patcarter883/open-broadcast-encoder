# Phase 1: Memory Safety & Codec Correctness - Approach

**Explored:** 2026-05-05
**Status:** Ready for planning

<domain>
## Phase Boundary

Fix critical bugs that produce corrupted video and crash the application. This phase delivers:

- **BUG-01**: Video encodes without corruption (use-after-free in `pull_video_buffer()`)
- **BUG-02**: Application survives encode/stop/restart without crashing (dangling global pointer)
- **BUG-03**: H.265 encoding uses correct parser element (`h265parse` not `h264parse`)
- **BUG-04**: NVENC AV1 encoding uses correct encoder element (`nvav1enc` not `x264enc`)
- **BUG-05**: Memory usage remains bounded during long encodes (unbounded stats vectors)

Phase 2 (separate) handles FLTK threading compliance. This phase focuses on memory safety and codec correctness only.

</domain>

<decisions>
## Implementation Decisions

### 1. Fix use-after-free in pull_video_buffer() (BUG-01)
**Chosen approach:** `gst_buffer_extract_dup()` to copy buffer contents into an owned `std::vector<uint8_t>`
**Alternatives considered:** Manual `memcpy` from mapped buffer, keeping `shared_ptr<GstBuffer>` reference
**Why this one:** `gst_buffer_extract_dup()` is the GStreamer-recommended pattern for extracting owned copies. It handles allocation internally, returns a fresh `uint8_t*` that the caller owns, and eliminates the entire class of use-after-free bugs. Manual memcpy would work identically but requires explicit size tracking. Keeping a shared_ptr reference would be more complex and still risks lifetime issues if the GStreamer pipeline state changes.

Specific decisions:
- `buffer_data.buf_data` changes from `uint8_t*` to `std::vector<uint8_t>` (owned copy)
- `buffer_data.buf_size` becomes redundant but kept for backward compatibility with callers
- `pull_video_buffer()` calls `gst_buffer_extract_dup()` before `gst_sample_unref()`
- `pull_audio_buffer()` gets the same fix (same bug pattern)
- `transport::send_buffer()` receives `const std::vector<uint8_t>&` instead of `buffer_data&`
- Caller in `run_loop()` passes `vidbuf.data()` and `vidbuf.size()` to transport

### 2. Fix encoder lifetime / dangling global pointer (BUG-02)
**Chosen approach:** Convert `encoder_ptr` from raw pointer to `std::shared_ptr<encode>` with proper reset on stop
**Alternatives considered:** `weak_ptr` pattern, `unique_ptr` with explicit shutdown protocol, state machine with explicit lifecycle
**Why this one:** The `library` struct already declares `std::shared_ptr<encode> encoder_ptr`. The current code in `main.cpp` uses `make_shared` but the struct member type needs to match. `shared_ptr` is the right choice because:
- `rist_stats_cb` callback reads `encoder_ptr` from `ctx.lib` — it needs to survive the callback if the encoder is being stopped
- Multiple threads access the encoder (run_loop reads buffers, rist_stats_cb sets bitrate)
- `shared_ptr` copy is atomic and thread-safe for the pointer itself
- Resetting the shared_ptr on stop is safe — `rist_stats_cb` already checks for null

Specific decisions:
- `library::encoder_ptr` is already `shared_ptr<encode>` — ensure type consistency
- `stop()` function resets `encoder_ptr` to nullptr after stopping the thread
- `run_loop()` checks `encoder_ptr` before calling `pull_video_buffer()`
- `rist_stats_cb` checks `encoder_ptr` before calling `set_encode_bitrate()` (already done)
- New encode instance created fresh on each start

### 3. Cap stats vectors at 1000 entries (BUG-05)
**Chosen approach:** `std::deque` with `erase(begin())` when size exceeds 1000
**Alternatives considered:** Circular buffer, `std::vector` with `erase(begin())`, ring buffer with fixed array
**Why this one:** `deque` is already specified in the SPEC. It provides O(1) push_back and O(n) erase from front, but with small constant factors. For 1000 entries at ~14Hz, erase runs ~14 times/second with negligible cost. A circular buffer would be more efficient but adds complexity (index tracking, overwrite logic) that is unnecessary for this scale. `vector::erase(begin())` would cause O(n) memmove on every entry.

Specific decisions:
- `cumulative_stats` changes from `std::vector<int>` to `std::deque<int>` for all 4 fields
- After each push_back in `got_rist_statistics()`, check if size > 1000 and erase front
- The running average computation using `std::accumulate` still works on deque
- 1000 entries at ~14Hz = ~72 seconds of stats window (sufficient for UI display)
- `stats::got_rist_statistics()` is the only place that pushes — single point of change

### 4. H.265 parser fix (BUG-03) — isolated one-line changes
**Chosen approach:** Direct string replacement in encoder build methods
**Alternatives considered:** Refactor all encoder methods into a single parameterized builder, extract parser element to a helper function
**Why this one:** The bugs are clearly identified as wrong string literals. Refactoring the entire encoder-switching logic (14 methods across encode.cpp) is out of scope for a bug-fix phase. The existing switch-based dispatch is clear and maintainable. One-line changes minimize risk of introducing new bugs.

Specific changes:
- `pipeline_build_amd_h265_encoder()`: line 248 `h264parse` → `h265parse`
- `pipeline_build_qsv_h265_encoder()`: line 276 `h264parse` → `h265parse`
- `pipeline_build_nvenc_h265_encoder()`: line 300 `h264parse` → `h265parse`
- `pipeline_build_software_h265_encoder()`: line 324 `h264parse` → `h265parse`

### 5. NVENC AV1 encoder fix (BUG-04) — isolated one-line change
**Chosen approach:** Direct string replacement of encoder element name
**Alternatives considered:** Refactor to use encoder/codec lookup table, add validation layer
**Why this one:** The bug is clear — `x264enc` (H.264 software encoder) used instead of `nvav1enc` (NVENC AV1 hardware encoder). This is a copy-paste error. Direct fix is lowest risk.

Specific change:
- `pipeline_build_nvenc_av1_encoder()`: line 307 `x264enc` → `nvav1enc`, adjust parameters for `nvav1enc` syntax

### 6. FLTK threading in stats callback — deferred to Phase 2
**Chosen approach:** No change in Phase 1
**Why:** The `stats::got_rist_statistics()` function already wraps UI updates in `ui.lock()/unlock()`. This is correct per the FLTK threading model. Phase 2 will audit all UI update paths. No changes needed here.

### Agent's Discretion
- Exact cap value for stats vectors (1000 specified in SPEC, can adjust if needed)
- Error handling for GStreamer pipeline parse failures during restart
- Whether to add pipeline state logging for debugging
- Exact `nvav1enc` parameters (research GStreamer docs for proper settings)
- Whether to add a `pipeline_cleaned_up` guard to prevent double-free on restart

</decisions>

<assumptions>
## Validated Assumptions

### Confirmed
- [confident] `gst_buffer_extract_dup()` returns caller-owned memory — no extra unref needed
- [confident] `std::shared_ptr` copy is thread-safe for the pointer itself (not the referent)
- [confident] `std::deque` is drop-in replacement for `std::vector` in `std::accumulate` calls
- [confident] FLTK UI locking in `stats::got_rist_statistics()` is already correct (lock/unlock present)
- [confident] The 4 H.265 parser bugs are all `h264parse` → `h265parse` (one per encoder vendor)

### Accepted (not challenged)
- [assuming] 1000-entry cap on stats vectors provides sufficient window (~72s at 14Hz)
- [assuming] `nvav1enc` accepts similar parameters to other nvenc elements (bitrate, rc-mode)
- [assuming] `library::encoder_ptr` as `shared_ptr<encode>` is the correct ownership model
- [assuming] The `buffer_data.buf_size` field can remain for backward compatibility
- [assuming] ASan + manual encode/stop/restart cycle is sufficient verification (no hardware needed)

### Corrected
- [corrected] SPEC mentions `weak_ptr` for encoder callback → `shared_ptr` is actually correct because the callback reads from a global that needs to survive concurrent access; `weak_ptr` would require re-locking the encoder on every callback which is overkill
- [corrected] Source files use `.cpp`/`.h` (not `.cppm` modules) — the AGENTS.md module table describes intended architecture but current code uses traditional headers

</assumptions>

<deferred>
## Deferred Ideas

- Runtime encoder/codec switching without pipeline restart — Phase 2 or future
- Multi-stream RIST bonding — explicitly out of scope (v2 feature per SPEC)
- Hardware-accelerated codec detection — explicitly out of scope (v2 feature per SPEC)
- SDP file loading (button exists but callback not wired) — explicitly out of scope per SPEC
- FLTK threading compliance audit — Phase 2
- Circular buffer for stats — over-engineering for 1000 entries
- Pipeline state machine for encode lifecycle — out of scope for bug fixes
- GStreamer element validation layer — nice-to-have, not a bug fix

</deferred>

---

*Phase: 01-memory-safety*
*Approach explored: 2026-05-05*
