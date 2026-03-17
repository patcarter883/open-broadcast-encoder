# AI Agent Guidelines for open-broadcast-encoder

## Project Overview

C++20 video streaming encoder with FLTK GUI, GStreamer pipelines, and RIST transport protocol.

**Input Sources**: SDP/RTP, NDI, MPEG-TS  
**Codecs**: H264, H265, AV1  
**Hardware Encoders**: AMD (AMF), Intel (QSV), NVIDIA (NVENC), Software (x264/x265/rav1e)

## Architecture

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│   UI (FLTK) │────▶│  lib.cppm    │◀────│  main.cpp   │
│  ui.cppm    │     │  (types)     │     │ (orchestrator)│
└─────────────┘     └──────────────┘     └─────────────┘
                            │                    │
         ┌──────────────────┼────────────────────┤
         ▼                  ▼                    ▼
┌─────────────┐    ┌──────────────┐    ┌─────────────┐
│ encode.cppm │    │ transport.cppm│   │ndi_input.cppm│
│ (GStreamer) │    │  (RIST net)  │   │ (NDI monitor)│
└─────────────┘    └──────────────┘    └─────────────┘
                            │
                            ▼
                    ┌──────────────┐
                    │  stats.cppm  │
                    │ (bitrate adj)│
                    └──────────────┘
```

### Key Design Decisions

1. **C++20 Modules**: All source files use `.cppm` extension. Module names differ from file paths (e.g., `source/lib/lib.cppm` exports as `library` module).

2. **Callback Pattern**: Components use function pointer callbacks for logging and events:
   ```cpp
   using log_func_ptr = void (*)(const std::string& msg);
   ```

3. **FLTK Threading**: All UI updates from background threads require locking:
   ```cpp
   ui.lock();        // Fl::lock()
   // ... update UI ...
   ui.unlock();      // Fl::unlock(); Fl::awake();
   ```

## Build Commands

```bash
# Standard build
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build

# Developer mode (requires CMakeUserPresets.json)
cmake --preset=dev
cmake --build --preset=dev
ctest --preset=dev

# Code formatting
cmake --build build -t format-fix
cmake --build build -t format-check

# Run executable
cmake --build build -t run-exe
```

## C++20 Modules

| File | Module Name | Import Statement |
|------|-------------|------------------|
| `source/lib/lib.cppm` | `library` | `import library;` |
| `source/encode/encode.cppm` | `encode` | `import encode;` |
| `source/transport/transport.cppm` | `transport` | `import transport;` |
| `source/ui/ui.cppm` | `ui` | `import ui;` |
| `source/ndi_input/ndi_input.cppm` | `ndi_input` | `import ndi_input;` |
| `source/stats/stats.cppm` | `stats` | `import stats;` |

## Coding Conventions

### clang-format
- Column limit: 80
- Indent width: 2 spaces
- Brace wrapping: Custom (after functions, classes, namespaces)
- Break binary operators: NonAssignment
- Pointer alignment: Left

Always run `cmake --build build -t format-fix` before committing.

### Naming Patterns
- **Types**: `snake_case` for structs (`input_config`, `encode_config`, `output_config`)
- **Classes**: `lowercase` (`encode`, `transport`, `user_interface`, `ndi_input`)
- **Functions**: `snake_case` (`run_encode_thread`, `setup_rist_sender`, `refresh_ndi_devices`)
- **Enums**: `snake_case` with `enum class` (`input_mode`, `codec`, `encoder`)

## Key Dependencies

| Dependency | Purpose | Source |
|------------|---------|--------|
| **GStreamer 1.24+** | Media pipelines | pkg-config (gstreamer-1.0, gstreamer-app-1.0, etc.) |
| **FLTK** | GUI framework | external/fltk (git submodule) |
| **NDI** | Network Device Interface | external/fltk + system NDI SDK |
| **rist-cpp** | RIST protocol wrapper | external/rist-cpp (git submodule) |
| **fmt** | Formatting library | vcpkg |

## GStreamer Pipeline Construction

The `encode` module builds pipelines dynamically using string formatting:

```cpp
pipeline_str = std::format(
    "udpsrc port={} ! tsdemux name=demux "
    "demux. ! queue ! videoconvert ! {} ! tsmux ",
    port, encoder_element);
datasrc_pipeline = gst_parse_launch(pipeline_str.c_str(), &error);
```

Pipeline elements are named for later retrieval:
```cpp
video_encoder = gst_bin_get_by_name(GST_BIN(datasrc_pipeline), "videncoder");
```

### Pipeline Building Pattern

The `encode` class uses a builder pattern with separate methods for each pipeline section:
- `pipeline_build_source()` - Input source (udpsrc, sdpsrc, ndisrc)
- `pipeline_build_sink()` - Output sink (appsink)
- `pipeline_build_video_demux()` - Video demuxer
- `pipeline_build_audio_demux()` - Audio demuxer
- `pipeline_build_video_encoder()` - Hardware/software encoder selection
- `pipeline_build_amd_encoder()`, `pipeline_build_qsv_encoder()`, etc.

## Data Flow

1. **Input** → `ndi_input` or SDP/MPEGTS source → GStreamer demuxer
2. **Decode** → `videoconvert`/`audioconvert` → Encoder element
3. **Encode** → `h264parse`/`h265parse`/`av1parse` → `mpegtsmux`
4. **Output** → `appsink` → `pull_video_buffer()` → `transport::send_buffer()`
5. **Transport** → RIST sender → Network destination

## Adaptive Bitrate Logic

`stats::got_rist_statistics()` adjusts encode bitrate based on RIST link quality:
- If quality drops: reduce bitrate proportionally
- If quality is 100%: gradually increase toward max bitrate
- Updates UI with current/cumulative statistics

## External Submodules

Initialize before building:
```bash
git submodule update --init --recursive
```

Submodules:
- `external/fltk` - GUI toolkit
- `external/rist-cpp` - RIST protocol C++ wrapper
- `external/sdp-tools-cpp` - SDP parsing (currently disabled in CMakeLists.txt)

## Developer Workflows

### CMake Presets

Create `CMakeUserPresets.json` for developer mode:
```json
{
  "version": 2,
  "configurePresets": [{
    "name": "dev",
    "binaryDir": "${sourceDir}/build/dev",
    "inherits": ["dev-mode", "vcpkg", "ci-linux"],
    "cacheVariables": {"CMAKE_BUILD_TYPE": "Debug"}
  }],
  "buildPresets": [{
    "name": "dev",
    "configurePreset": "dev",
    "configuration": "Debug"
  }],
  "testPresets": [{
    "name": "dev",
    "configurePreset": "dev",
    "configuration": "Debug"
  }]
}
```

### Developer Mode Targets

Available when `open-broadcast-encoder_DEVELOPER_MODE` is enabled:
- `coverage` - Generate coverage reports
- `docs` - Build documentation with Doxygen
- `format-check` / `format-fix` - clang-format linting
- `run-exe` - Run the executable
- `spell-check` / `spell-fix` - codespell linting

### RIST URL Format

RIST URLs follow this pattern:
```
rist://{host}:{port}?bandwidth={bw}&buffer-min={min}&buffer-max={max}&rtt-min={min}&rtt-max={max}&reorder-buffer={buf}&timing-mode=2
```

Example:
```
rist://127.0.0.1:5000?bandwidth=6000&buffer-min=245&buffer-max=5000&rtt-min=40&rtt-max=500&reorder-buffer=240&timing-mode=2
```

## SDP Input Format

SDP strings follow RFC 4566 format. Example for raw video:
```
v=0
o=- 1443716955 1443716955 IN IP4 127.0.0.1
s=st2110 stream
t=0 0
a=recvonly

m=video 20000 RTP/AVP 102
c=IN IP4 127.0.0.1/8
a=rtpmap:102 raw/90000
a=fmtp:102 sampling=YCbCr-4:2:2; width=1920; height=1080; exactframerate=30000/1000; depth=10; TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN;
a=mediaclk:direct=0
a=ts-refclk:ptp=IEEE1588-2008:00-02-c5-ff-fe-21-60-5c:127
```

## NDI Input

NDI input uses GStreamer's `ndisrc` element:
```cpp
pipeline_str = std::format(
    "ndisrc do-timestamp=true ndi-name=\"{}\" ! ndisrcdemux name=demux ",
    input_c.selected_input);
```

Device discovery uses `gst_device_monitor`:
```cpp
device_monitor = gst_device_monitor_new();
gst_device_monitor_add_filter(device_monitor, "Video/Source", caps);
gst_device_monitor_start(device_monitor);
```

## RIST Transport

The `transport` module wraps `rist-cpp` for RIST protocol:
- `setup_rist_sender()` - Initialize sender with URL configuration
- `send_buffer()` - Send video buffer data
- `set_log_callback()` / `set_statistics_callback()` - Event callbacks

RIST settings:
- `mLogLevel = RIST_LOG_DEBUG`
- `mProfile = RIST_PROFILE_ADVANCED`

## Stats Module

The `stats` module handles RIST statistics and adaptive bitrate:
- `got_rist_statistics()` - Process RIST stats and adjust bitrate
- Tracks bandwidth, retransmitted packets, total packets
- Updates UI with cumulative statistics