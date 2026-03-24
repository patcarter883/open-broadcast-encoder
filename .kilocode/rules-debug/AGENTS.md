# AGENTS.md - Debug Mode

This file provides guidance to agents when debugging in this repository.

## Critical Rules

- **Use cmake-skill for all CMake operations**
- **Use context7 MCP for GStreamer debugging**

## GStreamer Debugging

### Environment Variables
```bash
GST_DEBUG=3                    # Warning level
GST_DEBUG=4                    # Info level
GST_DEBUG=GST_PIPELINE:5       # Pipeline-specific debug
GST_DEBUG_DUMP_DOT_DIR=/tmp    # Export pipeline graphs as DOT files
```

### Pipeline String Inspection
The full pipeline string is logged before parsing via [`log(this->pipeline_str)`](source/encode/encode.cpp:428). Check the encode log display in the UI.

### Error Handling Location
Pipeline errors are caught via the GStreamer bus message loop in [`play_pipeline()`](source/encode/encode.cpp:449):
```cpp
void encode::handle_gst_message_error(GstMessage* message)
{
  GError* err;
  gchar* debug_info;
  gst_message_parse_error(message, &err, &debug_info);
  log(std::format("Error received from element {}: {}",
                  GST_OBJECT_NAME(message->src), err->message));
}
```

### Common Issues
- Parse errors indicate malformed pipeline strings - check the logged pipeline string
- Missing elements: ensure GStreamer plugins installed (e.g., `gstreamer1.0-plugins-bad` for NDI)
- State change failures: check if elements are properly linked

## FLTK UI Debugging

### Thread Safety
UI updates from background threads require proper locking. Missing `Fl::awake()` causes UI to freeze:
```cpp
// CORRECT:
Fl::lock();
// ... update UI ...
Fl::unlock();
Fl::awake();       // Don't forget this!

// WRONG (will cause hangs):
Fl::lock();
// ... update UI ...
Fl::unlock();
// Missing Fl::awake()!
```

### Log Buffers
UI uses `Fl_Text_Buffer` for log displays. Check buffer contents via:
- `transport_log_display` / `encode_log_display` in [`ui.h`](source/ui/ui.h:65-66)
- Callbacks: `transport_log_append()` / `encode_log_append()`

## RIST Transport Debugging

### RIST URL Format
```
rist://{host}:{port}?bandwidth={bw}&buffer-min={min}&buffer-max={max}&rtt-min={min}&rtt-max={max}&reorder-buffer={buf}&timing-mode=2
```

Example:
```
rist://127.0.0.1:5000?bandwidth=6000&buffer-min=245&buffer-max=5000&rtt-min=40&rtt-max=500&reorder-buffer=240&timing-mode=2
```

### Statistics Callback
Bitrate adjustment is triggered via RIST statistics callback in [`main.cpp`](source/main.cpp:58):
```cpp
static auto rist_stats_cb(const rist_stats& stats)
{
  if (stats::got_rist_statistics(stats, &app.stats, app.encode_config, ui)) {
    ptr_encoder->set_encode_bitrate(app.stats.current_bitrate);
  }
}
```

## Build/Debug Commands

```bash
# Run the executable
cmake --build build -t run-exe

# Developer mode with debug symbols
cmake --preset=dev
cmake --build --preset=dev
```

## Adaptive Bitrate Algorithm

Location: [`stats::got_rist_statistics()`](source/stats/stats.h:12)

Key behavior:
- Quality drop → Reduce bitrate proportionally
- Quality 100% and below max → Increase gradually
- Rate limiting: Delta divided by 2
- Bounds: 1000 kbps minimum, configured maximum
