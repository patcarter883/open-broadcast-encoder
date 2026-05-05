# Architecture Research: Video Streaming Encoder Domain

**Domain**: C++20 video streaming encoder systems with GStreamer pipelines  
**Researched**: 2026-05-05

## Component Map

### Core Types (lib)
**Purpose**: Shared configuration and state types that all other components depend on.  
**Responsibilities**: 
- `input_config`, `encode_config`, `output_config` structs for user settings
- `buffer_data` struct for passing encoded video between components
- `cumulative_stats` for tracking network statistics
- `library` global state container with `is_running` flag and shared run flag
**Depends on**: Nothing  
**Used by**: All 6 layers (encode, transport, ui, ndi_input, stats, main)

### Encoding Pipeline (encode)
**Purpose**: GStreamer pipeline construction, execution, and buffer extraction.  
**Responsibilities**:
- Pipeline string construction via `pipeline_build_*` methods
- Hardware encoder selection (AMD/QSV/NVENC/Software × H264/H265/AV1)
- Video/audio buffer pulling from appsink elements
- Bitrate adjustment via GStreamer property updates
- Bus message handling for errors and EOS
**Depends on**: `source/lib/lib.h`, GStreamer libraries  
**Used by**: `main.cpp`, `stats.cpp` (for bitrate adjustment)

### Transport (transport)
**Purpose**: RIST protocol network transport for sending encoded video.  
**Responsibilities**:
- RIST sender initialization with URL configuration
- Buffer transmission via `rist_sender->sendData()`
- Callback forwarding for logging and statistics
**Depends on**: `source/lib/lib.h`, `source/url/url.h`, rist-cpp/RISTNet.h  
**Used by**: `main.cpp` (transport loop)

### UI (ui)
**Purpose**: FLTK desktop interface for configuration and monitoring.  
**Responsibilities**:
- Widget hierarchy (choices, inputs, buttons, log displays)
- Menu item arrays with enum values stored as user_data
- Thread-safe UI updates via `Fl::lock()`/`Fl::unlock()`
- Log display for encode/transport output
**Depends on**: `source/lib/lib.h`, FLTK  
**Used by**: `main.cpp`, `stats.cpp` (UI updates from background thread)

### NDI Input (ndi_input)
**Purpose**: NDI device discovery and preview capability.  
**Responsibilities**:
- GStreamer device monitor for NDI source discovery
- Device name refresh for UI population
- NDI preview pipeline creation
**Depends on**: `source/lib/lib.h`, GStreamer libraries  
**Used by**: `main.cpp`, `ui.cpp` (device refresh callback)

### Statistics & Adaptive Bitrate (stats)
**Purpose**: RIST feedback loop for automatic bitrate adjustment.  
**Responsibilities**:
- Quality-based bitrate calculation algorithm
- Cumulative statistics tracking (bandwidth, packets, bitrate)
- UI statistics display updates with thread locking
**Depends on**: `source/lib/lib.h`, `source/ui/ui.h`, rist-cpp  
**Used by**: `main.cpp` (RIST statistics callback)

### URL Parsing (url)
**Purpose**: RIST endpoint URL parsing.  
**Responsibilities**:
- Host/port/path/query extraction from URL strings
**Depends on**: Nothing  
**Used by**: `transport.cpp` (RIST URL construction)

## Key Patterns

### Pattern: Layered Modular Monolith
**What**: Single executable with clear layer boundaries, shared global state, and callback-based communication.  
**When**: Use for desktop applications with real-time processing requirements where simplicity trumps distributed architecture.  
**Example**:
```cpp
// Global state container passed by reference
library app;  // Holds all config + state
encode encoder(app.input_cfg, app.encode_cfg, app.run_flag, &log_callback);
```

### Pattern: GStreamer Pipeline String Builder
**What**: Pipeline constructed as `std::format` string, parsed at runtime by GStreamer.  
**When**: Dynamic pipeline construction based on runtime configuration (codec, encoder, input type).  
**Example**:
```cpp
// Double-dispatch: encoder vendor -> codec combination
switch (encode_c.selected_encoder) {
  case encoder::nvenc: pipeline_build_nvenc_encoder(); break;
  case encoder::amd: pipeline_build_amd_encoder(); break;
  // ...12 total encoder combinations (4 vendors × 3 codecs)
}
```

### Pattern: RIST Statistics Feedback Loop
**What**: Closed-loop adaptive bitrate where RIST network quality metrics drive encoder adjustments.  
**When**: Real-time streaming over unreliable networks where manual bitrate tuning is impractical.  
**Example**:
```cpp
// stats.cpp: Quality-based bitrate adjustment
if (quality_dropped) {
  adjBitrate = current_bitrate * (new_quality / previous_quality);
  bitrateDelta = adjBitrate - current_bitrate;
}
```

### Pattern: FLTK Thread-Safe UI Updates
**What**: All cross-thread UI updates wrapped in `Fl::lock()` / `Fl::unlock()` / `Fl::awake()`.  
**When**: Background processing threads (encoding, stats) updating UI widgets created in main thread.  
**Example**:
```cpp
// stats.cpp: UI update from RIST callback (runs on background thread)
ui.lock();
ui.bandwidth_output->value(bandwidth_str.c_str());
ui.unlock();  // Fl::unlock() + Fl::awake()
```

## Anti-Patterns

- **[Unbounded vector growth]**: Using `std::vector` for statistics without size limits causes memory exhaustion over time. → Use `boost::circular_buffer` or fixed-size ring buffer.
- **[Dangling pointer callbacks]**: Raw function pointers (`encode* ptr_encoder`) can dangle when object is destroyed. → Use `std::function` with `weak_ptr` or document lifetime requirements clearly.
- **[Use-after-free in buffer handling]**: `pull_video_buffer()` returns `info.data` after `gst_buffer_unmap` (via sample unref). → Return copied buffer or manage lifetime explicitly.
- **[Wrong GStreamer codec elements]**: Using `h264parse` for H.265 output, `x264enc` for NVENC AV1. → Verify element names against `gst-inspect-1.0` output.

## Component Boundaries

| Component A | Component B | Data Flow Direction | Shared Contract |
|-------------|-------------|---------------------|-----------------|
| main | encode | `input_config` → | Passes config by ref, receives `buffer_data` |
| encode | transport | `buffer_data` → | Pull from appsink, push to send_buffer |
| transport | stats | `rist_stats` → | RIST callback triggers stats processing |
| stats | ui | stats values → | UI updates via `lock()`/`unlock()` |
| ui | main | callbacks → | UI callbacks mutate config structs in-place |
| ndi_input | main | device names → | Refresh devices for UI population |

**Build Order Implications**:
1. **Core Types (lib)** must exist first - all other layers import it
2. **Encoding Pipeline** depends on Core Types + GStreamer available
3. **Transport** depends on Core Types + URL parsing + rist-cpp
4. **UI** depends on Core Types + FLTK available
5. **NDI Input** depends on Core Types + GStreamer available
6. **Statistics** depends on Core Types + UI header (circular dependency via forward declaration)
7. **URL Parsing** is independent, used only by Transport

**Hard-to-Reverse Architectural Decisions**:
1. **Global state pattern** (`library app` struct) - Changing to dependency injection would require touching every file
2. **Callback-based communication** - Moving to pub/sub or event system requires new infrastructure
3. **Raw pointer encoder reference** (`encode* ptr_encoder`) - Must be removed to fix dangling pointer; requires lifetime management redesign
4. **Unbounded stats vectors** - Memory leak will grow indefinitely; requires bounded buffer redesign with data eviction policy

---

*Confidence: HIGH (Analysis verified against GStreamer documentation, FFmpeg plugin reference, and OBS architecture patterns)*