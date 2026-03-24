# AGENTS.md - Architect Mode

This file provides guidance to agents when designing architecture for this repository.

## Critical Constraints

- **Use cmake-skill for all build system changes**
- **Use clear-thought MCP for architecture decisions**

## Architecture Overview

C++20 video streaming encoder with layered architecture:

```
┌─────────────────────────────────────────┐
│              UI Layer (FLTK)            │
│         Thread-safe UI updates          │
├─────────────────────────────────────────┤
│           Application Layer             │
│      Global state in main.cpp           │
│  (library, transporter, ui, encoder)    │
├─────────────────────────────────────────┤
│           Pipeline Layer                │
│    GStreamer pipeline construction      │
│    Dynamic element selection            │
├─────────────────────────────────────────┤
│           Transport Layer               │
│    RIST protocol via rist-cpp           │
│    Adaptive bitrate control             │
└─────────────────────────────────────────┘
```

## Non-Obvious Design Patterns

### Global State Management
Cross-component communication uses global instances in [`main.cpp`](source/main.cpp):
```cpp
library app;                          // Shared config/state
std::unique_ptr<transport> transporter;
user_interface ui;
encode* ptr_encoder;                  // For bitrate callback
```

This pattern is used because:
- FLTK callbacks require static/global context
- RIST callbacks need access to encoder for bitrate adjustment
- UI updates happen from multiple threads

### Callback Pattern for Logging
Components use function pointer callbacks for logging:
```cpp
using log_func_ptr = void (*)(const std::string& msg);
```

This decouples components from UI while allowing log aggregation.

### Pipeline Element Naming
Pipeline elements are named for runtime retrieval:
```cpp
pipeline_str = std::format("... ! {} name=videncoder ! ...", encoder_element);
video_encoder = gst_bin_get_by_name(GST_BIN(pipeline), "videncoder");
```

Required for dynamic bitrate adjustment.

### Threading Model

```
Main Thread          Worker Thread(s)
───────────          ───────────────
FLTK UI loop    ←──  encode thread
                     (GStreamer)
                     
RIST stats cb   ←──  RIST internal
                     (adaptive bitrate)
                     
UI updates      ←──  Background
     ↑               (with Fl::lock/unlock/awake)
     └──────────────┘
```

### Adaptive Bitrate Flow

```
RIST Statistics Callback
         │
         ▼
┌─────────────────┐
│ Quality Changed?│──No──▶ Maintain
└────────┬────────┘
         │ Yes
         ▼
┌─────────────────┐
│ Quality Dropped?│──No──▶ Check increase
└────────┬────────┘
         │ Yes
         ▼
    Reduce bitrate
    (proportional)
         │
         ▼
  Update encoder
  via ptr_encoder
```

## Adding New Components

### New Input Source
1. Add enum to `input_mode` in [`lib.h`](source/lib/lib.h:12)
2. Add UI menu item in [`ui.h`](source/ui/ui.h:106)
3. Implement source in `pipeline_build_source()` in [`encode.cpp`](source/encode/encode.cpp:107)
4. Handle demux in video/audio demux methods

### New Encoder
1. Add enum to `encoder` in [`lib.h`](source/lib/lib.h:27)
2. Add UI menu item in [`ui.h`](source/ui/ui.h:189)
3. Implement `pipeline_build_<vendor>_<codec>_encoder()` in [`encode.cpp`](source/encode/encode.cpp)
4. Wire up switch case

### New Transport Protocol
Pattern would follow `transport` class:
- `setup_<protocol>_sender()` for initialization
- `send_buffer()` for data transmission
- Callback registration for logging/stats

## Key Design Decisions

1. **Globals for UI callbacks**: FLTK requires static callbacks, globals provide necessary context
2. **Function pointers for logging**: Decouples components while allowing UI log aggregation
3. **String-based pipeline building**: GStreamer pipelines built dynamically via `std::format`
4. **Named pipeline elements**: Required for runtime bitrate adjustment
5. **Raw pointer to encoder**: `ptr_encoder` used for bitrate callback from RIST stats

## Technology Choices

| Technology | Purpose | Why |
|------------|---------|-----|
| GStreamer | Media processing | Industry standard, plugin ecosystem |
| FLTK | GUI | Lightweight, cross-platform |
| RIST | Transport | Professional broadcast standard |
| C++20 | Language | std::format, designated initializers |
| CMake | Build | Modern, preset support |

## Dependencies

- GStreamer 1.28+ (via pkg-config)
- FLTK (git submodule)
- rist-cpp (git submodule)
- NDI SDK (system)
- fmt (vcpkg)
