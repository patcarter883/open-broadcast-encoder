# Pitfalls

**Domain**: Video Streaming Encoder (C++20, GStreamer, FLTK, RIST)
**Researched**: 2026-05-05

## Pitfall 1: Use-After-Free in GStreamer Buffer Access

**What goes wrong**: The `pull_video_buffer()` function returns a `buffer_data` struct containing a raw pointer to GStreamer's internal buffer memory (`info.data`), but unrefs the sample immediately after mapping. The pointer becomes invalid as soon as GStreamer reuses or frees the buffer.

**Why it happens**: Developers assume `gst_buffer_map()` provides persistent memory, but GStreamer uses reference counting and buffer pools. The mapped memory is only valid until `gst_buffer_unmap()` is called or the buffer is unreffed. The current code:
```cpp
gst_buffer_map(buffer, &info, GST_MAP_READ);
gst_sample_unref(sample);  // Buffer may be freed/reused now!
return buffer_data{.buf_data = info.data};  // Dangling pointer!
```

**How to avoid**: 
- Copy buffer data before unref'ing the sample: `std::vector<uint8_t> copy(info.data, info.data + info.size)`
- Or use `gst_buffer_extract_dup()` to get owned memory
- Always unmap buffers before returning from functions that unref the parent sample
- Consider using `gst_buffer_ref()` if you need to hold the buffer longer

**Warning signs**: Random crashes in transport thread, corrupted video output, ASAN reports use-after-free at buffer access points.

## Pitfall 2: Wrong GStreamer Parser Elements for Codec/Type Mismatch

**What goes wrong**: H.265 streams are being parsed with `h264parse` instead of `h265parse`. NVENC AV1 uses `x264enc` software encoder instead of `nvav1enc`. These mismatched elements produce invalid bitstream output.

**Why it happens**: Copy-paste errors during encoder implementation. Developers assume all codecs work with the same parser elements, but:
- `h264parse` only understands H.264 NAL units - using it on H.265 produces garbage
- `x264enc` produces H.264, not AV1 - using it for NVENC AV1 means you're not actually getting hardware encoding

The code has:
```cpp
// Line 248-249: H.265 encoder uses h264parse (WRONG!)
"video/x-h265,framerate=60/1 ! h264parse config-interval=1"

// Line 306-309: NVENC AV1 uses x264enc (WRONG!)
"x264enc name=videncoder ... ! h264parse config-interval=1"
```

**How to avoid**:
- Create a codec-to-parser mapping table and enforce it at compile time with enums
- Write unit tests that validate pipeline strings contain correct parser elements
- Use `gst_element_factory_find()` to verify element availability before pipeline construction
- Code review checklist: "Does each codec use its matching parser (h264parse/h265parse/av1parse)?"

**Warning signs**: Pipeline errors about NAL unit parsing, receiver reports missing SPS/PPS, high CPU usage when expecting hardware encoding.

## Pitfall 3: Dangling Global Pointer to Encoder

**What goes wrong**: `lib.encoder_ptr` is a global `shared_ptr<encode>` that can be reset while background threads still reference it. The RIST statistics callback checks `if (ctx.lib.encoder_ptr != nullptr)` but the pointer can become dangling.

**Why it happens**: The `encode` object lifecycle isn't properly synchronized with the transport callback thread. When `stop()` is called:
1. `lib.encoder_ptr->stop_encode_thread()` is called
2. But RIST callback might fire between pointer null-check and actual cleanup
3. `set_encode_bitrate()` gets called on a being-destroyed object

**How to avoid**:
- Use `weak_ptr` in the callback and `lock()` it before use to detect expired objects
- Stop the transport callbacks before destroying the encoder
- Use a state machine with explicit "shutting down" state
- Never store raw/global `shared_ptr` to objects with background threads

**Warning signs**: Crashes in `rist_stats_cb` when stopping, double-free errors, segfault in `set_encode_bitrate()`.

## Pitfall 4: Unbounded Statistics Vectors Cause Memory Exhaustion

**What goes wrong**: `cumulative_stats` stores unlimited integers in vectors that grow every callback (lines 46-50 in stats.cpp):
```cpp
stats->bandwidth.push_back(statistics.stats.sender_peer.bandwidth);
stats->encode_bitrate.push_back(stats->current_bitrate);
stats->retransmitted_packets.push_back(statistics.stats.sender_peer.retransmitted);
stats->total_packets.push_back(statistics.stats.sender_peer.sent);
```

**Why it happens**: Over a long-running stream (hours/days), these vectors grow unbounded. After ~1M callbacks (19+ hours at 14Hz), memory usage explodes.

**How to avoid**:
- Use fixed-size circular buffers (ring buffers) with a maximum capacity
- Or only store aggregated statistics (cumulative sums, running averages)
- Or implement a sliding window: `if (vector.size() > MAX_WINDOW) { vector.erase(vector.begin(), vector.begin() + CHUNK); }`
- Set memory limits and monitor: `if (stats->bandwidth.size() > 100000) { /* prune */ }`

**Warning signs**: Gradual memory growth over time, eventually OOM kill, slow statistics accumulation.

## Pitfall 5: FLTK Threading Violations (UI Updates Without Locking)

**What goes wrong**: FLTK requires `Fl::lock()/Fl::unlock()` for all UI updates from background threads. The `got_rist_statistics` function updates UI widgets directly without locking.

**Why it happens**: Developers assume callback functions run on the main thread, but RIST callbacks are invoked from librist's internal thread. FLTK assertions or race conditions occur.

**How to avoid**:
- Always wrap UI updates in `ui.lock(); /* update */; ui.unlock();`
- Consider using `Fl::awake()` with a callback for thread-safe UI updates
- Audit all callback functions: "Does this run on background thread?"
- Use thread-sanitizer to catch violations

**Warning signs**: Intermittent UI corruption, FLTK assertion failures, X11 errors, race condition crashes.

---
*These pitfalls should inform verification criteria in the roadmap.*