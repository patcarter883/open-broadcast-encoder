# Codebase Architecture

**Analysis Date:** 2026-04-27

## Pattern Overview

Overall: **Modular Monolith with Strategy Pattern**

Key characteristics:
- Single executable with clearly separated subsystems, each built as a static library
- Strategy pattern for transport layer (`transport_base` abstract interface with RIST/SRT/RTMP concrete implementations)
- Global mutable shared state for cross-component communication (`library app` singleton)
- GStreamer pipeline-as-string construction for media processing
- FLTK event-loop UI with threaded background work and `Fl::lock()/Fl::unlock()/Fl::awake()` for thread safety

## Layers (Boundaries)

### Layer: Main / Orchestration
- Purpose: Application bootstrap, lifecycle coordination, global state management
- Location: `source/main.cpp`
- Owns: Global object instantiation (`library app`, `transport_manager`, `statistics_aggregator`, `user_interface`, `encode`, `ndi_input`), log callback wiring, event loop setup, run/stop lifecycle
- Does NOT own: Business logic (delegates to subsystems), UI rendering, pipeline construction, transport implementation
- Depends on: `stats.h`, `encode.h`, `transport.h`, `transport/transport_manager.h`, `transport/buffer_distributor.h`, `stats/statistics_aggregator.h`, `lib/lib.h`, `ndi_input.h`, `ui.h`
- Used by: Nothing — this is the entry point, the top of the call graph

### Layer: Configuration / Shared Types
- Purpose: Defines all enums, structs, and value types shared across subsystems
- Location: `source/lib/lib.h` (definitions), `include/common.h` (legacy/early definitions)
- Owns: Enums (`input_mode`, `codec`, `encoder`, `transport_protocol`), value structs (`buffer_data`, `shared_buffer`, `cumulative_stats`, `stream_stats`, `stream_config`, `input_config`, `encode_config`, `output_config`, `library`), `library` constructor and `log_append()`
- Does NOT own: Pipeline logic, UI rendering, transport I/O, statistics computation
- Depends on: Nothing (self-contained header)
- Used by: Every other layer — this is the most-included file in the codebase

### Layer: Input Sources
- Purpose: Discovers and previews video input sources (NDI, SDP, MPEGTS, test pattern)
- Location: `source/ndi_input/ndi_input.h`, `source/ndi_input/ndi_input.cpp`
- Owns: NDI device discovery via GStreamer `GstDeviceMonitor`, NDI preview pipeline, SDP input mode handling, test pattern preview
- Does NOT own: Video encoding, transport delivery, UI display of results (reports via callbacks)
- Depends on: `lib/lib.h`, GStreamer (NDI plugin, device monitoring)
- Used by: `main.cpp` (via `ndi_input` global), UI (via `refresh_ndi_devices` callback)

### Layer: Encode (GStreamer Pipeline)
- Purpose: Builds and runs GStreamer pipelines for video/audio encoding with hardware or software encoders
- Location: `source/encode/encode.h`, `source/encode/encode.cpp`
- Owns: Pipeline construction (`build_pipeline`), GStreamer element management, encoder selection (AMD/QSV/NVENC/software × H.264/H.265/AV1), bitrate adjustment, buffer pulling from appsink, error/EOS handling
- Does NOT own: Transport delivery (pushes to `buffer_distributor`), UI updates (uses callback), input source setup (delegated to pipeline building methods)
- Depends on: `lib/lib.h`, GStreamer (`gst/gst.h`, `gst/app/gstappsink.h`)
- Used by: `main.cpp` (as the `encode` instance), `stats.h` (for bitrate callback)

### Layer: Transport (Distribution & Delivery)
- Purpose: Distributes encoded buffers to multiple output destinations over various network protocols
- Location: `source/transport/` (directory)
- Owns:
  - `buffer_distributor` (single-to-many buffer fan-out, zero-copy via `shared_ptr`)
  - `transport_manager` (factory for concrete transports, lifecycle management)
  - `transport_base` (abstract interface for protocol implementations)
  - `rist_transport` (RIST via `RISTNetSender` from `rist-cpp`)
  - `srt_transport` (SRT — stub/not yet connected)
  - `rtmp_transport` (RTMP — stub/not yet connected)
  - Legacy `transport` class (original RIST-only sender, used in legacy path)
- Does NOT own: Buffer creation (incoming from encoder), UI display (reports stats via callbacks)
- Depends on: `lib/lib.h`, `transport_base.h`, RISTNet headers, `url/url.h`
- Used by: `main.cpp` (via `g_transport_manager`), UI (via `set_transport_manager` for multi-output)
- Data flow direction: Encoder -> `buffer_distributor::distribute()` -> per-transport `send_buffer()` -> network

### Layer: Statistics & Bitrate Control
- Purpose: Aggregates per-stream network statistics and computes consensus bitrate recommendations
- Location: `source/stats/stats.h`, `source/stats/stats.cpp`, `source/stats/statistics_aggregator.h`, `source/stats/statistics_aggregator.cpp`
- Owns:
  - `stats::got_rist_statistics()` — single-stream stat processing with auto-bitrate adjustment logic and UI update
  - `statistics_aggregator` — multi-stream aggregation, consensus bitrate calculation (quality-weighted), stale stream pruning, callback notification
- Does NOT own: Network I/O, encoding (reports to encoder via bitrate setter callback)
- Depends on: `lib/lib.h`, `RISTNet.h`, `ui/ui.h` (for single-stream path)
- Used by: `main.cpp` (via `g_stats_aggregator`), RIST transport (via stats callback)
- Data flow direction: RIST transport -> stats callback -> `statistics_aggregator::update_stream_stats()` -> consensus calculation -> bitrate callback -> `encode::set_encode_bitrate()`

### Layer: UI (FLTK)
- Purpose: Graphical configuration and monitoring interface
- Location: `source/ui/ui.h`, `source/ui/ui.cpp`, `source/ui/output_stream_widget.h`, `source/ui/output_stream_widget.cpp`, `source/ui/add_output_dialog.h`
- Owns: FLTK widget construction (`Fl_Double_Window`, `Fl_Flex`, `Fl_Grid` layouts), input configuration controls (protocol selection, codec/encoder choice, bitrate input), output address input, stats display grid, log text displays, multi-output stream widgets, add-output dialog
- Does NOT own: Video encoding, network I/O, pipeline construction (configures via shared `library app` state and callbacks)
- Depends on: `lib/lib.h`, FLTK headers, `transport/transport_manager.h` (for multi-output integration)
- Used by: `main.cpp` (via `ui` global, `ui.show()`, `ui.run_ui()`)
- Threading: All UI updates from background threads use `Fl::lock()` -> update -> `Fl::unlock()` -> `Fl::awake()`

### Layer: URL Parsing (Utility)
- Purpose: RFC 3986 compliant URL parsing for transport address strings
- Location: `source/url/url.h`, `source/url/url.cc`
- Owns: URL scheme, host, port, path, query parameter parsing
- Does NOT own: Transport logic (only provides parsing utility)
- Depends on: Standard C++ library only
- Used by: `transport.cpp`, `rist_transport.cpp`

### Layer: External Dependencies
- Location: `external/` (submodules)
- `external/fltk/` — FLTK GUI toolkit (embeddedled as `SYSTEM` include)
- `external/rist-cpp/` — RIST C++ API wrapper (`RISTNetSender`), bundled as `SYSTEM`
- `external/sdp-tools-cpp/` — SDP session description builder/parser (included but subdirectory commented out in CMake)

## Entry Points

Entrypoint: Application Main
- Location: `source/main.cpp`
- Triggers: Process launch (CLI)
- Responsibilities: GStreamer initialization, global object instantiation, callback wiring (log callbacks, stats callbacks), NDI device monitoring startup, UI initialization and event loop execution

Entrypoint: UI Event Loop
- Location: `source/ui/ui.cpp` -> `user_interface::run_ui()` (calls `Fl::run()`)
- Triggers: FLTK widget events (button clicks, choice selections, etc.)
- Responsibilities: Process all user interactions, dispatch to callback functions bound via `init_ui_callbacks()`

## Data Flow (Canonical Flows)

Flow: Encode -> Transport Pipeline
1. `main.cpp` launches `run_loop()` thread which creates `encode` and calls `encoder.run_encode_thread()`
2. `encode` builds and starts a GStreamer pipeline, pulls encoded video frames via `pull_video_buffer()`
3. `main.cpp` loop calls `g_transport_manager.get_distributor().distribute_legacy(vidbuf)` for each frame
4. `buffer_distributor` converts `buffer_data` to `shared_buffer` (zero-copy via shared_ptr with release deleter)
5. `buffer_distributor` iterates all connected transports, calls `transport->send_buffer(shared_buffer_ptr)` on each
6. Each concrete transport (`rist_transport`, etc.) sends data over the network via its protocol implementation
7. Transport stats callbacks fire, notifying `statistics_aggregator` which computes consensus bitrate and calls back to `encode::set_encode_bitrate()`

Flow: Configuration to Pipeline
1. User selects input protocol via `choice_input_protocol` menu in UI
2. UI callback sets `app.input_config.selected_input_mode`
3. On "Start Encode", `run()` creates `encode` instance with current `app.input_config` and `app.encode_config`
4. `encode::build_pipeline()` reads these configs to construct GStreamer pipeline string
5. `encode::parse_pipeline()` parses and launches the pipeline

State management:
- Global mutable state via `library app` instance in `main.cpp` — config structs and runtime stats
- Encoder and transport access via global pointers (`ptr_encoder`, `g_transport_manager`)
- Thread synchronization via `Fl::lock()` for UI and mutexes in `buffer_distributor`, `transport_manager`, `statistics_aggregator`

## Key Abstractions

Abstraction: Transport Protocol Strategy
- Purpose: Abstract network delivery protocol so buffer distributor works identically regardless of RIST/SRT/RTMP
- Examples: `source/transport/transport_base.h`, `source/transport/rist_transport.h`, `source/transport/srt_transport.h`, `source/transport/rtmp_transport.h`
- Pattern: Strategy pattern — `transport_base` defines virtual interface (`initialize`, `start`, `stop`, `send_buffer`, `is_connected`, `get_stats`), factory in `transport_manager` creates concrete type based on `transport_protocol` enum

Abstraction: Buffer Distribution
- Purpose: Fan-out a single encoded stream to multiple output destinations
- Examples: `source/transport/buffer_distributor.h`, `source/transport/buffer_distributor.cpp`
- Pattern: Pub-sub / multicast with `shared_ptr` for zero-copy buffer sharing across concurrent consumers

Abstraction: NDI Input Source
- Purpose: Discover and preview NDI sources on the network
- Examples: `source/ndi_input/ndi_input.h`, `source/ndi_input/ndi_input.cpp`
- Pattern: GStreamer device monitor running in background thread, device list refreshed on demand

## Error Handling Strategy

Strategy: Log-through-callbacks with pipeline error reporting
- GStreamer errors are captured in `encode::handle_gst_message_error()` which calls the `log_func` callback
- Log functions route to `ui.encode_log_append()` or `ui.transport_log_append()` via global function pointers
- Transport implementations use `log_message()` which routes through `m_log_callback` function object
- Examples: `source/encode/encode.cpp:446` (GST errors), `source/transport/transport_base.cpp:21` (transport logs), `main.cpp:27` (encode_log), `main.cpp:32` (transport_log)

Strategy: Callback-based error recovery
- Transport send failures report via `send_callback` with `success` boolean and error message
- Statistics callback reports per-stream quality for bitrate auto-adjustment

## Cross-Cutting Concerns

Logging:
- Log function pointer type (`log_func_ptr`) passed through component constructors
- Transport layer uses `std::function<void(const std::string&)>` callbacks
- Examples: `source/encode/encode.cpp:519`, `source/transport/transport_base.cpp:21`, `main.cpp:27-35`

Validation:
- Minimal input validation — mostly relies on GStreamer pipeline parsing errors
- URL parsing done via `homer6::url` in transport layer
- Transport protocol selection via enum (type-safe at compile time)

Threading:
- Main thread: UI event loop (`Fl::run()`)
- Background threads: encode pipeline thread, NDI monitor thread, transport stats threads
- All UI thread cross-talk uses `Fl::lock()` -> update -> `Fl::unlock()` -> `Fl::awake()` pattern (see `source/ui/ui.cpp:490-499`)

## Change Routing (Where To Add New Code)

When making a change, follow these rules:

| Change type | Add/modify here | Do NOT do this | Example paths |
|---|---|---|---|
| New input source (e.g. SRT input, RTP input) | New class in `source/` (e.g. `source/srt_input/srt_input.h`) or extend `encode::pipeline_build_source()` | Add to `ndi_input` or mix input logic into `encode` | `source/srt_input/srt_input.h`, `source/encode/encode.cpp:35` |
| New encoder (e.g. new hardware encoder) | Add enum variant to `encoder` in `source/lib/lib.h`, add `pipeline_build_*_encoder()` and codec combo methods in `source/encode/encode.cpp`, add menu item in `source/ui/ui.cpp` | Change existing encoder implementations | `source/lib/lib.h:26`, `source/encode/encode.cpp:223-323`, `source/ui/ui.cpp:113-150` |
| New codec (e.g. new video format) | Add enum variant to `codec` in `source/lib/lib.h`, add combo method in `source/encode/encode.cpp`, add menu item in `source/ui/ui.cpp` | Change codec logic outside the switch dispatchers | `source/lib/lib.h:19`, `source/encode/encode.cpp:155-221` |
| New transport protocol | Implement new class extending `transport_base` in `source/transport/`, register in `transport_manager::create_transport()` switch, add enum to `transport_protocol` in `source/lib/lib.h` | Hardcode protocol-specific logic outside the strategy hierarchy | `source/transport/webrtc_transport.h`, `source/transport/transport_manager.cpp:17` |
| New output stream widget / UI element | Add `Fl_*` widget to `user_interface` class members in `source/ui/ui.h`, construct in `source/ui/ui.cpp` constructor, add callback in `init_ui_callbacks()` | Add widgets outside the `user_interface` class | `source/ui/ui.h:58-69`, `source/ui/ui.cpp:284-427` |
| New stats metric | Add field to `stream_stats` and `cumulative_stats` in `source/lib/lib.h`, update `statistics_aggregator`, update UI display | Add stats fields scattered across transport headers | `source/lib/lib.h:80-91`, `source/stats/statistics_aggregator.cpp:205` |
| New bitrate adjustment strategy | Modify `calculate_consensus_bitrate()` in `source/stats/statistics_aggregator.cpp` | Modify bitrate logic in `stats.cpp` (single-stream legacy path) | `source/stats/statistics_aggregator.cpp:96-144` |
| Shared type change (new struct/enum) | Modify `source/lib/lib.h` only | Define types in multiple headers (causes duplication with `include/common.h`) | `source/lib/lib.h` |
| GStreamer pipeline change | Modify methods in `source/encode/encode.cpp` (`pipeline_build_*` methods) | Build pipelines outside the `encode` class | `source/encode/encode.cpp:351-367` |
| Test addition | Add `TEST_CASE` in `test/source/open-broadcast-encoder_test.cpp` | Test inside source files | `test/source/open-broadcast-encoder_test.cpp` |

## Golden Files Per Layer

To find the most-imported file in each layer, inbound `#include` count was used as the signal: the more files import a header, the more stable and centrally understood that abstraction is.

| Layer | Golden File | Inbound Import Count | Why |
|-------|-------------|---------------------|-----|
| Configuration / Shared Types | `source/lib/lib.h` | 10+ | Defines every enum and struct used across the entire codebase; imported by every subsystem (encode, transport, stats, ui, ndi_input, main) |
| Encode | `source/encode/encode.h` | 1 (main.cpp) | Only main.cpp imports it directly, but it is the sole interface to the GStreamer encoding subsystem; all pipeline logic flows through this class |
| Transport | `source/transport/transport_base.h` | 4 (rist/srt/rtmp_transport + buffer_distributor) | Abstract interface that all concrete transports implement; most-included transport file, defines the strategy contract |
| Transport Lifecycle | `source/transport/transport_manager.h` | 1 (ui/ui.cpp) | Factory + coordinator for transports, holds the buffer_distributor; single import but central orchestration role |
| Buffer Distribution | `source/transport/buffer_distributor.h` | 1 (transport_manager) | Core fan-out logic; imported by transport_manager but the conceptual heart of multi-output |
| Statistics | `source/stats/stats.h` | 2 (main.cpp, stats.cpp) | Single-stream RIST stat processing with UI updates; gateway between transport stats and encoding bitrate |
| Stats Aggregation | `source/stats/statistics_aggregator.h` | 1 (main.cpp) | Multi-stream consensus computation; new aggregation layer replacing legacy single-stream stats |
| UI | `source/ui/ui.h` | 1 (main.cpp) | The entire FLTK interface; all UI state and callback bindings flow through this class |
| Input | `source/ndi_input/ndi_input.h` | 1 (main.cpp) | NDI-specific input source; only main.cpp uses it directly |
| Utility | `source/url/url.h` | 2 (transport.cpp, rist_transport.cpp) | URL parsing used by transport layer; narrow but essential interface |

## Architecture Diagram (Textual)

```
┌─────────────────────────────────────────────────────────┐
│                    source/main.cpp                       │
│  Global state: library app, g_transport_manager,        │
│  g_stats_aggregator, ui, ndi_input, ptr_encoder         │
└──────┬──────────────┬──────────────┬────────────────────┘
       │              │              │
       ▼              ▼              ▼
┌──────────┐   ┌────────────┐  ┌──────────────┐
│  UI       │   │  Encode     │  │ NDI Input    │
│ FLTK      │   │ GStreamer   │  │ Device monitor│
│ Widget    │   │ Pipeline    │  │ Preview      │
│ Event Loop│   │ Appsink     │  └──────────────┘
└──────┬────┘   │     ▲        └──────────────┬─┘
       │        │                           │
       │        ▼                           │
       │  ┌──────────┐                      │
       │  │buffer_data│                     │
       │  └────┬─────┘                      │
       │       │ distribute_legacy()        │
       │       ▼                            │
       │  ┌──────────────────────────┐      │
       │  │ buffer_distributor       │      │
       │  │ shared_buffer (zero-copy)│      │
       │  └───┬────┬────┬───────────┘      │
       │      │    │    │                   │
       ▼└──────┼────┼────┼───────────────────┘
       │      │    │    │
       ▼      ▼    ▼    ▼
┌────────────────────────────────────────────┐
│           Transport Layer                   │
│  risttransport_base (abstract interface)       │
│   ├── rist_transport  (RISTNetSender)       │
│   ├── srt_transport   (stub)               │
│   └── rtmp_transport  (stub)               │
└──────────────────┬─────────────────────────┘
                   │ stats callbacks
                   ▼
┌────────────────────────────────────────────┐
│        Statistics & Bitrate Control          │
│  statistics_aggregator                      │
│    ├── update_stream_stats()               │
│    ├── calculate_consensus_bitrate()       │
│    └── callback -> encode::set_bitrate()   │
└────────────────────────────────────────────┘
```

---

*Architecture analysis: 2026-04-27*
