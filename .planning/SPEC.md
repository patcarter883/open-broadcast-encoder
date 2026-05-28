# Open Broadcast Encoder

## What We're Building

Critical bug fixes for C++20 video streaming encoder to achieve stable, corruption-free video encoding with proper H.264/H.265/AV1 codec support and no crashes during normal encode/stop/restart cycles.

## Core Value

Video encodes without corruption and the application survives encode stop/restart cycles without crashing.

## Core Principles

1. **Memory Safety First** - Use AddressSanitizer and RAII patterns; no dangling pointers or use-after-free
2. **Correctness Before Features** - Fix broken fundamentals before adding capabilities
3. **Thread Safety Mandatory** - All UI updates from background threads use `Fl::lock()/unlock()`
4. **Testable Verification** - Every fix verified with specific observable outcomes

## Typed Data Schemas

```typescript
// Critical types for bug verification
type buffer_data = {
  buf_data: std::vector<uint8_t>;  // Fixed: was pointer, now owned buffer
};

type cumulative_stats = {
  bandwidth: std::deque<double>;        // Fixed: was unbounded vector, now capped
  encode_bitrate: std::deque<double>;     // Fixed: bounded deque
  retransmitted_packets: std::deque<int>; // Fixed: bounded deque
  total_packets: std::deque<int>;         // Fixed: bounded deque
};

type library = {
  input_config: input_config;
  encode_config: encode_config;
  output_config: output_config;
  cumulative_stats: cumulative_stats;
  is_running: std::atomic_bool;
};
```

## Capability & Security Gates

- **Destructive DB actions:** N/A - no database
- **External Purchases:** N/A - no external purchases
- **File system access:** Allowed for SDP input (future feature)
- **Network access:** Required for RIST streaming (existing capability)

## Requirements

### Validated (Existing -- already working)

- [x] **CORE-01**: User can select input protocol (SDP/NDI/MPEGTS) - existing
- [x] **CORE-02**: User can select codec (H264/H265/AV1) - existing
- [x] **CORE-03**: User can select encoder (AMD/NVENC/QSV/Software) - existing
- [x] **CORE-04**: UI displays logs for encode and transport - existing
- [x] **CORE-05**: NDI device discovery and preview - existing

### Must Have (v1) - Bug Fixes

- [ ] **BUG-01**: Video encodes without corruption [Done-When: Test with sample NDI input, verify output RIST stream has valid H.264 video that plays without artifacts or corruption]
- [ ] **BUG-02**: Application survives encode/stop/restart without crashing [Done-When: Start encode with any input, click Stop, click Start again - no segfault or crash]
- [ ] **BUG-03**: H.265 encoding uses correct parser element [Done-When: GStreamer pipeline log shows `h265parse` not `h264parse` for H.265 output]
- [ ] **BUG-04**: NVENC AV1 encoding uses correct encoder element [Done-When: GStreamer pipeline log shows `nvav1enc` not `x264enc` for NVENC AV1 output]
- [ ] **BUG-05**: Memory usage remains bounded during long encodes [Done-When: Encode for 10+ minutes, memory growth < 50MB (was unbounded growth)]

### Out of Scope

- SDP file loading (button exists but callback not wired) - deferred
- Runtime encoder switching without restart - deferred
- Multi-stream RIST bonding - v2 feature
- Hardware-accelerated codec detection - v2 feature
- File recording capability - beyond scope

## Constraints

- **[Tech]**: C++20 required - uses `std::format`, modules, coroutines not available
- **[Memory]**: AddressSanitizer detection required - all fixes verified under ASan
- **[Threading]**: FLTK requires `Fl::lock()/unlock()` for all cross-thread UI updates
- **[GStreamer]**: Pipeline elements must match codec (h264parse/h265parse/av1parse)

## Key Decisions

| Decision | Rationale | Date |
|----------|-----------|------|
| Fix use-after-free first | Video corruption blocks all functionality | 2026-05-05 |
| Replace buffer pointer with owned vector | Simplifies memory management, eliminates dangling pointer | 2026-05-05 |
| Use weak_ptr for encoder callback | Prevents crashes when encoder destroyed during RIST callback | 2026-05-05 |
| Cap stats vectors at 1000 entries | Prevents memory exhaustion, provides ~70s window at 14Hz | 2026-05-05 |

## Current State

- **Active Phase:** Phase 1 - Critical Bug Fixes ([-])
- **Last Completed:** Plan 01 implementation complete, verification pending
- **In Progress:** Phase 1 implementation — all 5 bug fixes applied
- **Decisions:** Prioritize memory safety and codec correctness
- **Blockers:** None


---

*Last updated: 2026-05-05 after research synthesis*