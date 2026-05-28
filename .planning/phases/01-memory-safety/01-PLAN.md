---
phase: 01-memory-safety
plan: 01
type: execute
wave: 1
runtime: opencode
assurance: cross_runtime_checked
depends_on: []
files-modified:
  - source/lib/lib.h
  - source/encode/encode.cpp
  - source/transport/transport.cpp
  - source/transport/transport.h
  - source/stats/stats.cpp
  - source/main.cpp
autonomous: true
requirements:
  - BUG-01
  - BUG-02
  - BUG-03
  - BUG-04
  - BUG-05
non_goals:
  - Do not add new features, codecs, encoders, or input protocols.
  - Do not refactor the encoder dispatch switch logic (14 methods stay as-is).
  - Do not add FLTK threading compliance fixes (Phase 2).
  - Do not add pipeline state machines or restart guards beyond what is needed.
  - Do not modify NDI discovery, SDP loading, or MPEG-TS parsing.
hard_boundaries:
  - Do not touch source/ui/ui.cpp, source/ui/ui.cppm, or any FLTK widget code.
  - Do not modify source/ndi_input/ or source/url/ files.
  - Do not change CMakeLists.txt, CMakePresets.json, or build system files.
  - Do not add new dependencies or modify vcpkg.json.
escalation_triggers:
  - Stop if nvav1enc element is unavailable on the target system (report and skip one-line fix).
  - Stop if buffer_data change breaks an unexpected caller (audit all references before proceeding).
  - Stop if the build fails due to GStreamer version incompatibility (document version and skip).
approval_gates:
  - Any change to the encoder dispatch switch logic in encode.cpp requires explicit approval.
  - Adding new helper functions or refactoring existing ones requires explicit approval.
anti_regression_targets:
  - H.264 encoding path must continue to work (no regression on existing correct paths).
  - AMD AV1 and QSV AV1 paths (already using av1parse) must not be affected.
  - Software AV1 path (rav1enc + av1parse) must not be affected.
  - Audio encoding (aac) path must not be affected.
  - Existing RIST transport URL construction must not change.
  - Stats adaptive bitrate algorithm must not change (only vector type changes).
known_unknowns:
  - Exact nvav1enc parameter syntax may differ from x264enc (need GStreamer doc check).
  - gst_buffer_extract_dup() availability in GStreamer 1.24+ (should be available since 1.4).
  - Whether the current GStreamer installation has nvav1enc plugin installed.
high_leverage_surfaces: []
second_pass_required: false
closure_claim_limit: Do not claim phase completion until the build succeeds, ASan runs clean, and the codec fixes produce valid pipeline strings. Memory boundedness (BUG-05) requires observing the deque cap in action, not just code inspection.
parallelism_budget:
  max_concurrent_plans: 1
  safe_parallelism: []
leverage:
  lost: Minimal ceremony overhead for planning vs. just coding the fixes.
  kept: Existing layered modular monolith architecture, global state pattern, callback-based communication, switch-based encoder dispatch.
  gained: Owned buffer data eliminates entire class of use-after-free bugs; bounded stats prevent memory exhaustion; correct parsers ensure valid output.
must_haves:
  truths:
    - pull_video_buffer() returns owned buffer data (std::vector<uint8_t>) not a dangling pointer.
    - H.265 pipeline strings contain h265parse (not h264parse) for all four encoder vendors.
    - NVENC AV1 pipeline string contains nvav1enc (not x264enc).
    - Application survives encode/stop/restart cycle without crash (encoder_ptr properly reset).
    - Stats vectors are bounded (std::deque with 1000-entry cap).
---

# Phase 01: Memory Safety & Codec Correctness - Plan 01

## Objective

Fix five critical bugs that produce corrupted video output and crash the application during normal encode/stop/restart cycles. This delivers a stable encoding foundation by eliminating use-after-free memory errors, correcting GStreamer codec element selection, and bounding memory growth.

## Context

- **Source code**: Traditional `.cpp`/`.h` files (not C++20 modules as described in AGENTS.md module table)
- **Branch**: `v2` with modifications to encode.cpp, lib.h, main.cpp, ui.cpp
- **Research**: Four research files (ARCHITECTURE.md, PITFALLS.md, STACK.md, SUMMARY.md) document the bugs and fix approaches
- **Approach decisions**: See `.planning/phases/01-memory-safety/01-APPROACH.md` for locked user-validated choices

## Requirements Covered

| Bug ID | Description | Fix Type |
|--------|-------------|----------|
| BUG-01 | Video encodes without corruption | Use-after-free fix in pull_video_buffer() |
| BUG-02 | Survives encode/stop/restart | Encoder pointer lifetime management |
| BUG-03 | H.265 uses correct parser | String replacement h264parse → h265parse |
| BUG-04 | NVENC AV1 uses correct encoder | String replacement x264enc → nvav1enc |
| BUG-05 | Memory bounded during long encodes | Stats vector capping with deque |

## Must-Haves

1. `buffer_data.buf_data` is `std::vector<uint8_t>` (owned copy, not raw pointer)
2. `pull_video_buffer()` and `pull_audio_buffer()` use `gst_buffer_extract_dup()`
3. `transport::send_buffer()` works with new `buffer_data` type
4. All four H.265 encoder methods use `h265parse`
5. NVENC AV1 encoder method uses `nvav1enc`
6. `encoder_ptr` reset to nullptr after stop
7. Stats vectors are `std::deque<int>` with 1000-entry cap

## Anti-Goals

- Do not refactor the 14 encoder build methods into a parameterized builder
- Do not add pipeline state machines or restart guards beyond encoder_ptr reset
- Do not modify any UI/FLTK code
- Do not add tests (no test infrastructure exists in this repo)
- Do not add new logging or error handling beyond what the fixes require

## Hard Boundaries

- No changes to `source/ui/`, `source/ndi_input/`, `source/url/`
- No changes to CMakeLists.txt, CMakePresets.json, or build system
- No changes to vcpkg.json or external submodules
- No changes to encoder dispatch switch logic (pipeline_build_amd_encoder, etc.)

## Evidence Contract

- Build succeeds with `cmake --build build` (no compilation errors)
- Pipeline log strings for H.265 contain `h265parse` (observable in encode log)
- Pipeline log string for NVENC AV1 contains `nvav1enc` (observable in encode log)
- `buffer_data` struct has `std::vector<uint8_t>` member (code inspection)
- Stats vectors use `std::deque` with size check (code inspection)

## Common Pitfalls

- **Forgetting pull_audio_buffer()**: The same use-after-free bug exists in pull_audio_buffer() — fix both functions identically.
- **Breaking send_buffer() signature**: If send_buffer() signature changes, main.cpp must be updated too. Keep the signature compatible or change all callers.
- **nvav1enc parameter mismatch**: nvav1enc may not accept `speed-preset` or `tune` parameters that x264enc uses. Use nvenc-compatible parameters (bitrate, rc-mode, preset).
- **H.265 parser in wrong place**: Only fix the parser element after H.265 encoder output — do not touch h264parse in H.264 encoder methods.
- **Deque erase performance**: Erasing from front of deque is O(n) but with small constant for 1000 entries. Do not use vector::erase(begin()) which causes O(n) memmove.

## Stop-And-Challenge

- If nvav1enc is not available on the target GStreamer installation, do not guess at parameters — report the issue and skip that one-line fix.
- If buffer_data has callers outside the files we're modifying (check grep for `buffer_data` across the entire source tree), stop and audit before proceeding.

## Approval Gates

- Any change to the encoder dispatch switch logic requires explicit user approval.
- Adding new helper functions or refactoring existing ones requires explicit user approval.

<checks>
<plan_check>
checker: self
checker_runtime: opencode
status: passed
blocking: false
notes: Planner self-check completed. All five bugs mapped to tasks. No cross-file contradictions.
</plan_check>
<plan_check>
checker: cross_runtime
checker_runtime: gsdd-plan-checker (gsdd-plan-checker subagent)
status: issues_found
cycle: 1
blocking: true
notes: Found 3 blockers (race condition, 10-min verification, anti-regression) and 4 warnings. Fixed.
</plan_check>
<plan_check>
checker: cross_runtime
checker_runtime: gsdd-plan-checker (gsdd-plan-checker subagent)
status: issues_found
cycle: 2
blocking: true
notes: Found 4 blockers (seq/ts_ntp, build verification, gst_buffer_extract_dup memory mgmt). Fixed.
</plan_check>
<plan_check>
checker: cross_runtime
checker_runtime: gsdd-plan-checker (gsdd-plan-checker subagent)
status: issues_found
cycle: 3
blocking: true
notes: Found 1 blocker (send_buffer signature vs APPROACH.md) and 2 warnings. Fixed.
</plan_check>
<plan_check>
checker: cross_runtime
checker_runtime: gsdd-plan-checker (gsdd-plan-checker subagent)
status: passed
cycle: 4
blocking: false
notes: Final cycle — only 1 trivial warning (parameter name in verify step). Fixed. Plan is ready for execution.
</plan_check>
</checks>

## Tasks

<task id="01-01" type="auto">
  <files>
    - MODIFY: source/lib/lib.h
    - MODIFY: source/encode/encode.cpp
    - MODIFY: source/transport/transport.cpp
    - MODIFY: source/transport/transport.h
    - MODIFY: source/main.cpp
  </files>
  <action>
    Fix BUG-01 (use-after-free) and BUG-02 (encoder lifetime).

   1. In `source/lib/lib.h`: Change `buffer_data` struct:
        - Replace `uint8_t* buf_data {}` with `std::vector<uint8_t> buf_data`
        - Keep `size_t buf_size {0}` (can derive from buf_data.size(), but keep for compat)
        - Remove unused `seq` and `ts_ntp` fields (currently uint64_t, never written or read in codebase; line 43-44 of lib.h)
        - Add `#include <vector>` if not already present

    2. In `source/encode/encode.cpp`: Fix `pull_video_buffer()` (line 487-501):
        Replace the current code:
        ```cpp
        GstMapInfo info;
        gst_buffer_map(buffer, &info, GST_MAP_READ);
        gst_sample_unref(sample);
        return buffer_data {.buf_size = info.size, .buf_data = info.data};
        ```
        With:
        ```cpp
        GstMapInfo info;
        gst_buffer_map(buffer, &info, GST_MAP_READ);
        uint8_t* raw = gst_buffer_extract_dup(buffer, 0, info.size);
        gst_buffer_unmap(buffer, &info);
        gst_sample_unref(sample);
        buffer_data result;
        result.buf_size = info.size;
        result.buf_data = std::vector<uint8_t>(raw, raw + info.size);
        g_free(raw);
        return result;
        ```
        Key points: gst_buffer_extract_dup() returns caller-owned memory that must be freed with g_free().
        Apply the same fix to `pull_audio_buffer()` (line 503-517) with identical pattern.

   3. In `source/transport/transport.h`: Change `send_buffer` signature:
        - From: `void send_buffer(buffer_data &buf, u_int16_t connection_id)`
        - To: `void send_buffer(const std::vector<uint8_t>& data, u_int16_t connection_id)`
        - Add `#include <vector>` to transport.h if not already present

    4. In `source/transport/transport.cpp`: Update `send_buffer` body:
        - Change from: `this->rist_sender->sendData(buf.buf_data, buf.buf_size, 0, virt_dst_port)`
        - To: `this->rist_sender->sendData(data.data(), data.size(), 0, virt_dst_port)`

    5. In `source/main.cpp`: Fix encoder lifetime in `stop()` function (line 86-92):
        - Before `ctx.lib.encoder_ptr->stop_encode_thread()`, call:
          `ctx.transporter->set_statistics_callback(nullptr);`
          This deregisters the RIST stats callback to prevent it from firing
          while the encoder is being destroyed.
        - After `ctx.lib.encoder_ptr->stop_encode_thread()`, add:
          `ctx.lib.encoder_ptr = nullptr;`
        - This ensures rist_stats_cb sees nullptr after stop, preventing use-after-free.
        - NOTE: The rist_stats_cb function already checks `if (ctx.lib.encoder_ptr != nullptr)`
          before dereferencing, so the null check provides a second layer of safety.

   6. In `source/main.cpp`: Update `run_loop()` (line 63-79):
        - Change `auto vidbuf = ctx.lib.encoder_ptr->pull_video_buffer()` to work with new buffer_data
        - Change `if (vidbuf.buf_size > 0)` to `if (!vidbuf.buf_data.empty())`
        - Change `ctx.transporter->send_buffer(vidbuf, 0)` to:
          `ctx.transporter->send_buffer(vidbuf.buf_data, 0)`
          (send_buffer now takes const std::vector<uint8_t>&, which matches vidbuf.buf_data type)
        - After creating the new encoder, re-register the RIST stats callback:
          `ctx.transporter->set_statistics_callback(&rist_stats_cb);`
          This is needed because stop() deregisters it to prevent the race condition.

    NOTE: The SPEC.md mentions "use weak_ptr for encoder callback" but the APPROACH.md
    corrects this to shared_ptr lifecycle (create on start, reset on stop). Use shared_ptr
    as specified in APPROACH.md — the weak_ptr suggestion was superseded by the approved
    approach. The shared_ptr is safe because: (a) rist_stats_cb checks nullptr before use,
    (b) callback is deregistered before reset, (c) shared_ptr copy is atomic.
  </action>
  <verify>
    - Run `cmake --build build -t format-fix` to ensure formatting is correct
    - Run `cmake --build build 2>&1 | head -50` to verify compilation succeeds (catches type mismatches from buffer_data change)
    - Verify buffer_data struct in lib.h has `std::vector<uint8_t> buf_data`
    - Verify pull_video_buffer uses gst_buffer_extract_dup() and g_free()
    - Verify pull_audio_buffer has same fix pattern
    - Verify transport send_buffer uses data.data() and data.size() (parameter renamed to data)
    - Verify main.cpp stop() resets encoder_ptr to nullptr after stop_encode_thread()
    - Verify main.cpp run_loop() checks !vidbuf.buf_data.empty()
    - Verify seq and ts_ntp fields removed from buffer_data (no longer needed)
  <done>
    buffer_data uses owned std::vector, pull_video_buffer/pull_audio_buffer copy before unref,
    transport send_buffer works with new type, encoder_ptr is reset on stop.
  </done>
</task>

<task id="01-02" type="auto">
  <files>
    - MODIFY: source/encode/encode.cpp
  </files>
  <action>
    Fix BUG-03 (H.265 parser) and BUG-04 (NVENC AV1 encoder) — isolated string replacements.

    1. In `source/encode/encode.cpp`, fix H.265 parser (h264parse → h265parse):
       - Line 248: `pipeline_build_amd_h265_encoder()`: Change `h264parse` to `h265parse`
       - Line 276: `pipeline_build_qsv_h265_encoder()`: Change `h264parse` to `h265parse`
       - Line 300: `pipeline_build_nvenc_h265_encoder()`: Change `h264parse` to `h265parse`
       - Line 324: `pipeline_build_software_h265_encoder()`: Change `h264parse` to `h265parse`

    2. In `source/encode/encode.cpp`, fix NVENC AV1 encoder:
       - Lines 304-309: `pipeline_build_nvenc_av1_encoder()`: Replace the entire encoder block
         from:
           `x264enc name=videncoder speed-preset=fast tune=zerolatency bitrate={} ! h264parse config-interval=1`
         to:
           `nvav1enc name=videncoder bitrate={} rc-mode=cbr preset=low-latency-hq ! av1parse config-interval=1`
       - Note: nvav1enc uses `rc-mode` (not `rate-control`) and `preset` (not `speed-preset`)
       - Also fix the trailing parser: `h264parse` → `av1parse` (already correct in the string, just replacing encoder)

    IMPORTANT: Do not change ANY other encoder methods. Only the four H.265 methods and
    the one NVENC AV1 method. H.264 methods keep h264parse. AMD AV1 and QSV AV1 already
    use av1parse and must not be touched.
  </action>
  <verify>
    - Verify pipeline_build_amd_h265_encoder contains `h265parse` (not h264parse)
    - Verify pipeline_build_qsv_h265_encoder contains `h265parse` (not h264parse)
    - Verify pipeline_build_nvenc_h265_encoder contains `h265parse` (not h264parse)
    - Verify pipeline_build_software_h265_encoder contains `h265parse` (not h264parse)
    - Verify pipeline_build_nvenc_av1_encoder contains `nvav1enc` (not x264enc)
    - Verify all four H.264 encoder methods still contain `h264parse` (no regression)
    - Verify AMD AV1 and QSV AV1 methods still contain `av1parse` (no regression)
  </verify>
  <done>
    All H.265 encoder methods use h265parse. NVENC AV1 uses nvav1enc. H.264 paths unchanged.
  </done>
</task>

<task id="01-03" type="auto">
  <files>
    - MODIFY: source/lib/lib.h
    - MODIFY: source/stats/stats.cpp
  </files>
  <action>
    Fix BUG-05 (unbounded stats vectors causing memory exhaustion).

   1. In `source/lib/lib.h`: Change `cumulative_stats` struct (lines 47-58):
        - Add `#include <deque>` (keep existing `#include <vector>` since buffer_data uses std::vector)
        - Change all four vector fields to deque:
         - `std::vector<int> bandwidth;` → `std::deque<int> bandwidth;`
         - `std::vector<int> retransmitted_packets;` → `std::deque<int> retransmitted_packets;`
         - `std::vector<int> total_packets;` → `std::deque<int> total_packets;`
         - `std::vector<int> encode_bitrate;` → `std::deque<int> encode_bitrate;`

    2. In `source/stats/stats.cpp`: Add bounds checking after each push_back (lines 46-50):
       - After `stats->bandwidth.push_back(...)`: add `if (stats->bandwidth.size() > 1000) stats->bandwidth.erase(stats->bandwidth.begin());`
       - After `stats->encode_bitrate.push_back(...)`: add `if (stats->encode_bitrate.size() > 1000) stats->encode_bitrate.erase(stats->encode_bitrate.begin());`
       - After `stats->retransmitted_packets.push_back(...)`: add `if (stats->retransmitted_packets.size() > 1000) stats->retransmitted_packets.erase(stats->retransmitted_packets.begin());`
       - After `stats->total_packets.push_back(...)`: add `if (stats->total_packets.size() > 1000) stats->total_packets.erase(stats->total_packets.begin());`

    Note: std::accumulate works identically on deque as on vector — no changes needed
    for the average computation (lines 52-67).
  </action>
  <verify>
    - Verify cumulative_stats in lib.h has `std::deque<int>` for all four fields
    - Verify lib.h includes `<deque>` (not just `<vector>`)
    - Verify stats.cpp has size > 1000 check after each of the four push_back calls
    - Verify std::accumulate calls still work (they accept any input iterator range)
    - Verify no other code in the project uses cumulative_stats fields that depend on vector-specific APIs
    - Run the application with a test input for 10+ minutes; monitor RSS memory with
      `top -p $(pgrep open-broadcast-encoder)` or `valgrind --tool=massif`; confirm
      memory growth is under 50MB
  </verify>
  <done>
    Stats vectors are bounded at 1000 entries using std::deque with erase-from-front policy.
    10+ minute runtime test confirms memory growth under 50MB.
  </done>
</task>

## Verification

- `cmake --build build` succeeds without errors
- `cmake --build build -t format-fix` passes with no formatting changes needed
- Code inspection confirms: buffer_data uses std::vector, H.265 uses h265parse, NVENC AV1 uses nvav1enc, stats uses std::deque with cap

## Success Criteria

1. [BUG-01] `pull_video_buffer()` returns `std::vector<uint8_t>` (observable: code inspection shows `gst_buffer_extract_dup()` usage)
2. [BUG-03] H.265 pipeline contains `h265parse` (observable: pipeline log string when H.265 is selected)
3. [BUG-02] Encoder pointer reset on stop (observable: code inspection shows `encoder_ptr = nullptr` in stop())
4. [BUG-04] NVENC AV1 pipeline contains `nvav1enc` (observable: pipeline log string when NVENC + AV1 selected)
5. [BUG-05] Stats vectors bounded at 1000 entries (observable: code inspection shows deque with size check)

## High-Leverage Review

No high-leverage surfaces touched. All changes are localized bug fixes with no architectural impact. Second pass not required.

## Leverage Review

- Lost: Minimal — slight overhead of planning ceremony
- Kept: Existing architecture, global state pattern, callback communication, encoder dispatch
- Gained: Eliminated use-after-free class of bugs, bounded memory growth, correct codec output

## Notes

- The `buffer_data.buf_size` field is kept for backward compatibility but can be derived from `buf_data.size()`. It may be removed in a future cleanup phase.
- The encoder_ptr = nullptr reset in stop() is the key fix for BUG-02. The rist_stats_cb already checks for nullptr before calling set_encode_bitrate, so this prevents the crash during encode/stop/restart cycles.
- The nvav1enc parameters (bitrate, rc-mode, preset) are based on nvenc element conventions. If the target GStreamer installation uses a different nvav1enc variant, parameters may need adjustment.
- 1000 entries at ~14Hz = ~72 seconds of stats window, which is sufficient for UI display and ABR algorithm decisions.
- ASan verification is recommended but not required for plan completion (no test infrastructure exists).
