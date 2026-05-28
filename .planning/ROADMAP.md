# Roadmap: Open Broadcast Encoder - Bug Fix Milestone

## Overview

Phase 1 fixes critical stability bugs that prevent the encoder from producing valid video output and crashing during normal operation. This delivers a working foundation for v1 feature completion.

## Phases (Status Legend: [ ] not started, [-] in progress, [x] complete)

- [-] **Phase 1: Memory Safety & Codec Correctness** - Fix use-after-free, dangling pointer, and wrong GStreamer elements
- [ ] **Phase 2: Threading Compliance** - Wrap all UI updates in FLTK locks, verify with thread sanitizer

## Phase Details

### Phase 1: Memory Safety & Codec Correctness

**Goal**: Fix critical bugs that produce corrupted video and crash the application
**Status**: [-]
**Requirements**: BUG-01, BUG-02, BUG-03, BUG-04, BUG-05
**Success Criteria**:
1. [Observable] `pull_video_buffer()` returns copied buffer data (not dangling pointer to unmapped memory)
2. [Observable] H.265 pipeline contains `h265parse`, NVENC AV1 contains `nvav1enc` (verified in encode log)
3. [Observable] Encode + stop + start cycle completes without segfault
4. [Observable] Application runs 10+ minutes with memory growth < 50MB

### Phase 2: Threading Compliance

**Goal**: Ensure all UI updates from background threads use FLTK locking
**Status**: [ ]
**Requirements**: BUG-02
**Success Criteria**:
1. [Observable] `rist_stats_cb` wraps UI updates in `ui.lock()/unlock()`
2. [Observable] Helgrind reports no race conditions in UI update paths
3. [Observable] Application runs 10+ minutes without UI corruption

---

*Created: 2026-05-05*