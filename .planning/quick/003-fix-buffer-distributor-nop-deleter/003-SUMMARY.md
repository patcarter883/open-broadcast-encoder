# Quick Task #003 — Fix no-op deleter on buffer distributor

## Summary

Fixed use-after-free in `buffer_distributor::distribute_legacy()`.

### Problem
`distribute_legacy()` wrapped a raw `uint8_t*` pointer in a `shared_ptr` with a no-op deleter `[](uint8_t*) {}`. This pointer came from `buffer_data` which was populated by `encode::pull_video_buffer()` — a function that calls `gst_sample_unref(sample)` before returning, invalidating the data pointer. Transports reading this buffer accessed freed memory.

### Fix
Replaced the no-op deleter pattern with a deep copy of the buffer data using `std::make_unique<uint8_t[]>` + `std::copy`. The transported data is now an independent allocation that survives the GStreamer buffer lifecycle.

### Files changed
- `source/transport/buffer_distributor.cpp` (lines 93-95)

### Verification
- Syntax check passed: `clang++ -std=c++20 -fsyntax-only` on modified file
- Build and runtime testing deferred to user (NDI SDK not available in this environment)
