# AGENTS.md - Code Mode

This file provides guidance to agents when working with code in this repository.

## Critical Build Rule

- **Use cmake-skill for ALL CMake operations** - Never call cmake directly via terminal

## Code Generation Rules

- **Use clear-thought MCP** for all code generation and modification tasks
- **Use context7 MCP** when dealing with GStreamer APIs

## Non-Obvious Coding Patterns

### FLTK Threading (Critical)
All UI updates from background threads MUST use this exact pattern:
```cpp
Fl::lock();        // NOT ui.lock()
// ... update UI ...
Fl::unlock();
Fl::awake();       // Required to wake UI thread
```

### UI Callback Pattern
FLTK callbacks use `FL_METHOD_CALLBACK_N` macros from `FL/fl_callback_macros.H`:
```cpp
FL_METHOD_CALLBACK_2(choice_input_protocol,
                     user_interface, this, choose_input_protocol,
                     input_config*, input_c,
                     FuncPtr, ndi_refresh_funcptr);
```

### Menu Item User Data
Menu items store enum values as `user_data()` for type-safe selection:
```cpp
// In ui.h - cast enum to long, store as void*
{"AMD", 0, 0, (void*)(static_cast<long>(encoder::amd)), ...}

// Retrieval - cast back to enum
auto val = static_cast<encoder>(
    reinterpret_cast<uintptr_t>(choice->mvalue()->user_data()));
```

### Global State Access
Cross-component communication uses globals in [`main.cpp`](source/main.cpp):
```cpp
library app;                          // Shared config/state
std::unique_ptr<transport> transporter;
user_interface ui;
encode* ptr_encoder;                  // For bitrate callback
```

### Include Order
Headers MUST be included in this order:
1. Standard library headers
2. Third-party headers (GStreamer, FLTK)
3. Project headers (lib.h first, then others)

### Pipeline Building Pattern
The `encode` class builds GStreamer pipelines using string formatting. Elements are named for later retrieval:
```cpp
pipeline_str = std::format("... ! {} name=videncoder ! ...", encoder_element);
video_encoder = gst_bin_get_by_name(GST_BIN(pipeline), "videncoder");
```

## Adding New Features

### Adding a New Encoder
1. Add enum value to [`encoder`](source/lib/lib.h:27) in `lib.h`
2. Add menu item to [`menu_choice_encoder`](source/ui/ui.h:189) in `ui.h`
3. Implement `pipeline_build_<vendor>_<codec>_encoder()` in [`encode.cpp`](source/encode/encode.cpp)
4. Wire up switch case in `pipeline_build_<vendor>_encoder()`

### Adding a New Input Source
1. Add enum value to [`input_mode`](source/lib/lib.h:12) in `lib.h`
2. Add menu item to [`menu_choice_input_protocol`](source/ui/ui.h:106) in `ui.h`
3. Implement source building in `pipeline_build_source()` in [`encode.cpp`](source/encode/encode.cpp:107)
4. Handle demux in `pipeline_build_video_demux()` and `pipeline_build_audio_demux()`

## Code Style

- Run `cmake --build build -t format-fix` before committing
- 80 column limit, 2-space indent
- `lower_case` for classes, functions, enums
- Left pointer alignment: `type* ptr`
- Brace wrapping after functions/classes/namespaces
