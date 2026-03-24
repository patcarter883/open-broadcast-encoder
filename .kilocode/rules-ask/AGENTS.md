# AGENTS.md - Ask Mode

This file provides guidance to agents when answering questions about this repository.

## Project Overview

C++20 video streaming encoder with FLTK GUI, GStreamer pipelines, and RIST transport.

- **Input Sources**: SDP/RTP, NDI, MPEG-TS
- **Codecs**: H264, H265, AV1
- **Hardware Encoders**: AMD (AMF), Intel (QSV), NVIDIA (NVENC), Software (x264/x265/rav1e)

## Architecture

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│   UI (FLTK) │────▶│    lib.h     │◀────│  main.cpp   │
│    ui.h     │     │  (types)     │     │(orchestrator)│
└─────────────┘     └──────────────┘     └─────────────┘
                            │                    │
         ┌──────────────────┼────────────────────┤
         ▼                  ▼                    ▼
┌─────────────┐    ┌──────────────┐    ┌─────────────┐
│ encode.h    │    │ transport.h  │   │ndi_input.h  │
│ (GStreamer) │    │  (RIST net)  │   │ (NDI monitor)│
└─────────────┘    └──────────────┘    └─────────────┘
                             │
                             ▼
                     ┌──────────────┐
                     │  stats.h     │
                     │ (bitrate adj)│
                     └──────────────┘
```

## MCP Usage Guidelines

- **context7 MCP**: Use for GStreamer API questions - provides latest documentation
- **clear-thought MCP**: Use for code analysis and architecture questions

## Key Dependencies

| Dependency | Purpose | Source |
|------------|---------|--------|
| GStreamer 1.28+ | Media pipelines | pkg-config |
| FLTK | GUI framework | external/fltk (submodule) |
| NDI | Network Device Interface | System NDI SDK |
| rist-cpp | RIST protocol | external/rist-cpp (submodule) |
| fmt | Formatting | vcpkg |

## Documentation References

- `HACKING.md` - Developer setup and CMake presets
- `BUILDING.md` - Build instructions
- `.clang-format` / `.clang-tidy` - Code style configs
- `cmake/` - Build system modules

## External Submodules

Initialize before building:
```bash
git submodule update --init --recursive
```

- `external/fltk` - GUI toolkit
- `external/rist-cpp` - RIST protocol C++ wrapper

## Data Flow

1. **Input** → `ndi_input` or SDP/MPEGTS source → GStreamer demuxer
2. **Decode** → `videoconvert`/`audioconvert` → Encoder element
3. **Encode** → `h264parse`/`h265parse`/`av1parse` → `mpegtsmux`
4. **Output** → `appsink` → `pull_video_buffer()` → `transport::send_buffer()`
5. **Transport** → RIST sender → Network destination

## File Organization

| File | Purpose |
|------|---------|
| `source/lib/lib.h` | Shared types and configuration |
| `source/encode/encode.h` | GStreamer pipeline management |
| `source/transport/transport.h` | RIST protocol wrapper |
| `source/ui/ui.h` | FLTK GUI |
| `source/ndi_input/ndi_input.h` | NDI source handling |
| `source/stats/stats.h` | Adaptive bitrate statistics |
| `source/url/url.h` | URL parsing (used by transport) |

Note: `source/url/` contains a non-module URL parsing library.

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
a=fmtp:102 sampling=YCbCr-4:2:2; width=1920; height=1080; ...
```
