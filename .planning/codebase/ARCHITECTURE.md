# Codebase Architecture

**Analysis Date:** 2026-04-28

## Pattern Overview

Overall: **Modular monolith with callback-driven event architecture.**

The project is a single C++20 executable (`open-broadcast-encoder`) organized as a modular monolith using C++20 modules (`.cppm`) alongside traditional pre-module header/implementation pairs (`.h`/`.cpp`). A top-level `main.cpp` orchestrates all components through function pointer callbacks and shared global state, with no middleware or external message bus.

Key characteristics:
- **C++20 Module-first architecture**: Six module units (`library`, `encode`, `transport`, `ui`, `ndi_input`, `stats`) with `import`/`export module` declarations in `.cppm` files. Each module also ships a traditional `.h`/`.cpp` pair (used by CMakeLists.txt build targets), resulting in dual-representation type definitions.
- **Callback pattern over polymorphism**: Cross-component communication uses function pointers (e.g., `log_func_ptr`, `rist_stats_cb`) rather than virtual interfaces or event emitters. See `source/main.cpp:23-55` for callback registration.
- **Global singleton state**: `source/main.cpp` declares global instances (`library app`, `user_interface ui`, `encode* ptr_encoder`) that all components access directly. There is no dependency injection or service container.
- **FLTK as the orchestrating thread**: The FLTK GUI (`source/ui/ui.cppm`) owns the application lifecycle (`Fl::run()`), while the GStreamer encode pipeline and RIST transport run on background threads. All UI updates from background threads use `Fl::lock()`/`Fl::unlock()` (see `source/ui/ui.cppm:554-562`).
- **GStreamer pipeline-as-string**: The encoder constructs GStreamer pipelines as formatted strings at runtime (`std::format`) and parses them via `gst_parse_launch`. Pipeline elements are retrieved by name via `gst_bin_get_by_name`. See `source/encode/encode.cppm:107-421`.

## Layers (Boundaries)

### Layer: Library / Types (Shared Foundation)
- **Purpose**: Define all shared type definitions (enums, structs) consumed across all modules. This is the lowest layer -- nothing imports it; everything imports it.
- **Location**: `source/lib/lib.cppm` (module), `source/lib/lib.h` (pre-module header), `source/lib/lib.cpp` (module definition)
- **Owns**:
  - `input_mode` enum (mpegts, sdp, ndi, none)
  - `codec` enum (h264, h265, av1)
  - `encoder` enum (amd, qsv, nvenc, software)
  - `buffer_data` struct (video/audio buffer metadata)
  - `cumulative_stats` struct (bandwidth, packets, bitrate tracking)
  - `input_config`, `encode_config`, `output_config` structs (runtime configuration)
  - `library` struct (global state holder: `is_running`, threads, configs, stats)
- **Does NOT own**: Any business logic, pipeline construction, UI rendering, or transport logic.
- **Depends on**: Nothing (zero `import` directives in `lib.cppm`)
- **Used by**: `encode`, `transport`, `ui`, `ndi_input`, `stats` (all five modules `import library`)

### Layer: Encode (GStreamer Pipeline)
- **Purpose**: Construct, manage, and run GStreamer media pipelines for video encoding. Supports 3 input modes × 4 encoder vendors × 3 codecs = 36 encoder configurations.
- **Location**: `source/encode/encode.cppm` (module), `source/encode/encode.h` (pre-module header), `source/encode/encode.cpp` (pre-module impl)
- **Owns**:
  - Pipeline string construction (`pipeline_build_source`, `pipeline_build_sink`, `pipeline_build_video_demux`, etc.)
  - GStreamer element management (datasrc_pipeline, video_encoder, video_sink, bus)
  - Message handling (ERROR, EOS)
  - Buffer extraction from appsink (`pull_video_buffer`)
  - Dynamic bitrate adjustment (`set_encode_bitrate` -> `g_object_set`)
- **Does NOT own**: Network transport (delegates to `transport`), UI display (delegates log output to callbacks), input device discovery (delegates to `ndi_input`).
- **Depends on**: `library` module (imports config types)
- **Used by**: `main.cpp` (direct instantiation), `stats` module (bitrate callback via `ptr_encoder->set_encode_bitrate`)

### Layer: Transport (RIST Network)
- **Purpose**: Send encoded video buffers over the network via the RIST protocol using the `rist-cpp` wrapper (`external/rist-cpp`).
- **Location**: `source/transport/transport.cppm` (module), `source/transport/transport.h` (pre-module header), `source/transport/transport.cpp` (pre-module impl)
- **Owns**:
  - RIST sender initialization (`setup_rist_sender`)
  - URL construction from `output_config` (supports multi-stream with port offsets)
  - Buffer transmission (`send_buffer` -> `rist_sender->sendData`)
  - Callback forwarding (log callback, statistics callback)
- **Does NOT own**: Bitrate logic (delegates to `stats`), pipeline construction (delegates to `encode`), UI rendering.
- **Depends on**: `library` module (imports `output_config`, `buffer_data`), `url` library (non-module, `source/url/url.h`)
- **Used by**: `main.cpp` (direct instantiation), `stats` module (via callback chain from RIST)

### Layer: UI (FLTK GUI)
- **Purpose**: Provide a graphical interface for configuring input, encoding, and output settings; displaying live statistics; and starting/stopping encoding.
- **Location**: `source/ui/ui.cppm` (module), `source/ui/ui.h` (pre-module header), `source/ui/ui.cpp` (pre-module impl)
- **Owns**:
  - FLTK widget hierarchy (`Fl_Double_Window`, `Fl_Flex`, `Fl_Choice`, `Fl_Input`, `Fl_Text_Display`)
  - Menu item definitions (input protocol, codec, encoder menus)
  - Callback binding (`init_ui_callbacks` using `FL_METHOD_CALLBACK_*` macros)
  - State synchronization (reading widget values into config structs)
  - Thread-safe UI updates (`lock()`/`unlock()` wrappers around `Fl::lock()`/`Fl::unlock()`)
  - Log display panels (transport log, encode log via `Fl_Text_Buffer`)
- **Does NOT own**: Pipeline logic, network communication, encoding. Only displays and configures.
- **Depends on**: `library` module (imports config types for callback signatures)
- **Used by**: `main.cpp` (direct instantiation), `stats` module (stat display updates)

### Layer: NDI Input (Device Discovery)
- **Purpose**: Discover and manage NDI network video sources using GStreamer's `ndisrc` and `gst_device_monitor`.
- **Location**: `source/ndi_input/ndi_input.cppm` (module), `source/ndi_input/ndi_input.h` (pre-module header), `source/ndi_input/ndi_input.cpp` (pre-module impl)
- **Owns**:
  - GStreamer device monitoring (`run_device_monitor` -> `gst_device_monitor_new`)
  - Device enumeration (`refresh_devices` -> `gst_device_monitor_get_devices`)
  - Preview pipeline (`preview` -> launches a separate GStreamer pipeline with autovideosink)
- **Does NOT own**: Main encoding pipeline, network transport, UI rendering.
- **Depends on**: `library` module (imports `input_config`)
- **Used by**: `main.cpp` (direct instantiation), `ui.cppm` (callback: `refresh_ndi_devices`)

### Layer: Stats (Adaptive Bitrate)
- **Purpose**: Process RIST link quality statistics and automatically adjust the encoder bitrate to maintain quality.
- **Location**: `source/stats/stats.cppm` (module), `source/stats/stats.h` (pre-module header), `source/stats/stats.cpp` (pre-module impl)
- **Owns**:
  - Adaptive bitrate algorithm (`got_rist_statistics`)
  - Cumulative statistics tracking (online averages for bandwidth, encode bitrate, retransmitted packets)
  - UI stat display updates (bandwidth, link quality, RTT, packets)
  - Bitrate bounds enforcement (1000 kbps minimum, configured maximum)
- **Does NOT own**: RIST protocol implementation, pipeline construction, UI widget management.
- **Depends on**: `library` module (config types, `cumulative_stats`), `ui` module (stat display updates)
- **Used by**: `main.cpp` (called from RIST statistics callback), `transport` module (statistics callback chain)

### Layer: URL Parsing (Non-module Utility)
- **Purpose**: Parse RIST URLs for host, port, and query parameters. A vendored third-party library (`homer6/url v0.3.0`, MIT License).
- **Location**: `source/url/url.h`, `source/url/url.cc`
- **Owns**: URL parsing (RFC 3986 compliant), scheme/host/port/path/query extraction
- **Does NOT own**: Transport logic, RIST protocol
- **Depends on**: Nothing (pure C++20, no external dependencies)
- **Used by**: `transport` module only (`source/transport/transport.cppm:88`)

## Entry Points

### Entrypoint: `main()`
- **Location**: `source/main.cpp`
- **Triggers**: OS process launch (executable: `open-broadcast-encoder`)
- **Responsibilities**:
  1. Initialize GStreamer (`gst_init`, line 115)
  2. Start NDI device monitor (`ndi.run_device_monitor()`, line 117)
  3. Initialize FLTK UI (`ui.init_ui()`, line 118)
  4. Wire callbacks (`ui.init_ui_callbacks`, lines 119-126)
  5. Display UI and enter main loop (`ui.show()`, `ui.run_ui()`, lines 127-128)
  6. Global state setup: `library app` (line 18), `transport* transporter` (line 19), `user_interface ui` (line 20), `encode* ptr_encoder` (line 21), `ndi_input ndi` (line 33)

### Entrypoint: `run()` (Start Encoding)
- **Location**: `source/main.cpp:75-78`
- **Triggers**: User clicks "Start Encode" button in UI -> `ui.start()` -> `run()`
- **Responsibilities**: Spawns `run_loop` thread which instantiates `encode`, calls `run_encode_thread()`, then enters the main buffer-pull loop.

### Entrypoint: `run_transport()` (Setup RIST)
- **Location**: `source/main.cpp:85-91`
- **Triggers**: Callback wired in `ui.init_ui_callbacks` (line 125)
- **Responsibilities**: Creates `transport` instance, sets log/stats callbacks, configures RIST sender with `output_config`.

### Entrypoint: `preview_input()`
- **Location**: `source/main.cpp:93-111`
- **Triggers**: User clicks "Preview Input" button
- **Responsibilities**: Delegates to `ndi.preview()` for NDI source preview; SDP/MPEGTS have no preview implementation yet.

## Data Flow (Canonical Flows)

### Flow 1: Video Encoding Pipeline
1. **Input acquisition**: Data enters via one of three sources:
   - SDP/RTP: `sdpsrc` element in GStreamer pipeline (`source/encode/encode.cppm:132`)
   - NDI: `ndisrc` element via GStreamer device monitor (`source/ndi_input/ndi_input.cppm:91`)
   - MPEGTS: `udpsrc` receiving UDP multicast (`source/encode/encode.cppm:126`)
2. **Demux & decode**: Input-specific demuxer separates video/audio streams, `videoconvert`/`audioconvert` normalize formats (`source/encode/encode.cppm:159,176`)
3. **Encoding**: Encoder element selected by `encode_c.encoder` × `encode_c.codec` (36 combinations). Pipeline string built in `source/encode/encode.cppm:194-408`
4. **Mux**: Encoded video and AAC audio multiplexed via `mpegtsmux` (`source/encode/encode.cppm:152`)
5. **Buffer extraction**: `pull_video_buffer()` calls `gst_app_sink_pull_sample` on `video_sink` element (`source/encode/encode.cppm:536-550`), returns `buffer_data`
6. **Transport**: `main.cpp:70` calls `transporter->send_buffer(vidbuf, 0)` to push buffer over RIST network

**Direction**: Input Source -> GStreamer Demux -> GStreamer Encoder -> appsink -> pull_video_buffer() -> RIST transport -> Network

### Flow 2: Adaptive Bitrate Adjustment
1. **Statistics gathering**: RIST sender invokes `stats_cb_func` periodically (`source/transport/transport.cppm:75-79`)
2. **Callback chain**: `stats_cb_func` -> C function pointer `statistics_callback` -> `rist_stats_cb` in `main.cpp:50-55`
3. **Stats processing**: `stats::got_rist_statistics()` processes link quality, computes bitrate delta (`source/stats/stats.cppm:17-88`)
4. **Bitrate application**: If bitrate changed, `ptr_encoder->set_encode_bitrate()` applies new bitrate to encoder element (`main.cpp:53`)
5. **UI update**: Same function updates all stat display widgets (`source/stats/stats.cppm:67-86`)

**Direction**: RIST Network -> rist-cpp stats callback -> main.cpp rist_stats_cb -> stats::got_rist_statistics() -> encoder.set_encode_bitrate() -> UI display update

**State management**:
- `cumulative_stats` in `library::stats` holds running vectors and online averages (`source/lib/lib.cppm:43-55`)
- `stats->previous_quality` tracks quality between samples for change detection
- Bitrate clamped to [1000, maxBitrate] kbps range
- FLTK lock/unlock protects UI widget access from background thread

## Key Abstractions

### Abstraction: Configuration Structs
- **Purpose**: Plain data aggregates that flow through all layers (UI -> main.cpp -> encode/transport)
- **Examples**: `source/lib/lib.cppm:57-77` (input_config, encode_config, output_config)
- **Pattern**: `struct` with string fields (parsed by caller), no validation at definition site. Same structs duplicated in `include/common.h`, `source/common.h`, `source/lib/lib.h`, and `source/lib/lib.cppm` (see Architecture Concerns below).

### Abstraction: Callback Function Pointers
- **Purpose**: Decouple components without introducing inheritance or event systems
- **Examples**: 
  - `log_func_ptr` in `source/encode/encode.cppm:17`
  - `rist_log_cb` in `source/main.cpp:42-48`
  - `rist_stats_cb` in `source/main.cpp:50-55`
- **Pattern**: C-style function pointers passed through constructors or setter methods. Main.cpp acts as the callback router.

### Abstraction: GStreamer Pipeline Builder
- **Purpose**: Dynamically construct pipeline strings based on input mode, encoder vendor, and codec selection
- **Examples**: `source/encode/encode.cppm:107-421` (pipeline_build_* methods)
- **Pattern**: Builder pattern with method chaining. `build_pipeline()` calls each `pipeline_build_*()` method in sequence, appending to `pipeline_str`. Switch cases select vendor-specific encoder elements. Two-level dispatch: vendor switch -> codec switch -> specific encoder element.

### Abstraction: Adaptive Bitrate Algorithm
- **Purpose**: Maintain link quality by automatically adjusting encoder output bitrate
- **Examples**: `source/stats/stats.cppm:17-88`
- **Pattern**: Proportional adjustment on quality changes, gradual increase on perfect quality. Uses online averaging algorithm for cumulative statistics.

## Error Handling Strategy

**Strategy: Callback-based error reporting through log channels.**

- GStreamer pipeline errors are caught via `gst_bus_timed_pop` message loop (`source/encode/encode.cppm:449-534`). Errors are parsed with `gst_message_parse_error` and forwarded through the `log_func_ptr` callback to the UI log display.
- RIST transport errors use the same callback pattern through `rist_log_cb` (`source/main.cpp:42-48`) -> UI transport log.
- NDI preview errors are handled inline in `ndi_input::preview()` with direct log calls (`source/ndi_input/ndi_input.cppm:111-129`).
- No exception-based error recovery is used -- errors set flags (`encoder_running = false`) or return early.
- Pipeline parse failures log the raw pipeline string for debugging (`source/encode/encode.cppm:428-434`).

**Examples**: 
- `source/encode/encode.cppm:500-513` (handle_gst_message_error)
- `source/encode/encode.cppm:516-519` (handle_gst_message_eos)
- `source/ndi_input/ndi_input.cppm:107-129` (preview error handling)

## Cross-Cutting Concerns

### Logging
- **Approach**: Callback function pointers funnel all logging to UI text displays
- **Transport log**: `source/ui/ui.cppm:530-536` (`transport_log_append`) -> `Fl_Text_Display`
- **Encode log**: `source/ui/ui.cppm:543-546` (`encode_log_append`) -> `Fl_Text_Display`
- **RIST log**: `source/main.cpp:42-48` (`rist_log_cb`) -> transport log
- **Encode log**: `source/main.cpp:23-26` (`encode_log`) -> encode log
- Examples: `source/encode/encode.cppm:573-578`, `source/ndi_input/ndi_input.cppm:144-148`

### Configuration
- **Approach**: Flat structs in `source/lib/lib.cppm`, populated by UI callbacks, shared as references
- **Validation**: Minimal -- string fields (bitrate, address) are parsed with `std::stoi` only when consumed (`source/stats/stats.cppm:22`)
- **Defaults**: All structs have sensible defaults defined at member level

### Threading
- **Approach**: FLTK requires explicit lock/unlock for any UI access from background threads
- **UI lock wrapper**: `source/ui/ui.cppm:554-562` (`lock()`, `unlock()`)
- **Used by**: `source/stats/stats.cppm:67-86` (stat display updates from RIST callback thread)
- **Background threads**: encode thread (GStreamer pipeline), NDI monitor thread, preview thread, main loop buffer-pull thread

## Change Routing (Where To Add New Code)

When making a change, follow these rules:

| Change type | Add/modify here | Do NOT do this | Example paths |
|---|---|---|---|
| New input source (e.g., SRT, WebRTC) | Add enum to `input_mode` in `source/lib/lib.cppm:12-18`, add menu item to `menu_choice_input_protocol` in `source/ui/ui.cppm:106`, implement `pipeline_build_source()` case in `source/encode/encode.cppm:123-144`, implement preview in `source/ndi_input/ndi_input.cppm:86-142` | Add input logic to main.cpp or transport | `source/lib/lib.cppm`, `source/encode/encode.cppm`, `source/ndi_input/ndi_input.cppm` |
| New codec (e.g., VP9) | Add enum value to `codec` in `source/lib/lib.cppm:20-25`, add menu item to `menu_choice_codec` in `source/ui/ui.cppm:159`, implement `pipeline_build_<vendor>_<codec>_encoder()` methods in `source/encode/encode.cppm`, wire into vendor switches | Add codec logic to stats or transport | `source/lib/lib.cppm`, `source/ui/ui.cppm`, `source/encode/encode.cppm` |
| New hardware encoder vendor (e.g., Intel) | Add enum value to `encoder` in `source/lib/lib.cppm:27-33`, add menu item to `menu_choice_encoder` in `source/ui/ui.cppm:189`, implement `pipeline_build_<vendor>_encoder()` and `<vendor>_<codec>` methods in `source/encode/encode.cppm` | Add encoder to UI-only without pipeline code | `source/lib/lib.cppm`, `source/ui/ui.cppm`, `source/encode/encode.cppm` |
| New UI widget / panel | Add widget member to `user_interface` class in `source/ui/ui.cppm:27-104`, add to `user_interface()` constructor in `source/ui/ui.cppm:228-516`, bind callback in `init_ui_callbacks()` in `source/ui/ui.cppm:705`, update `layout()` if needed | Add widgets directly in main.cpp | `source/ui/ui.cppm` |
| New RIST transport feature | Add to `output_config` in `source/lib/lib.cppm:68-77` (if config), implement in `setup_rist_sender()` in `source/transport/transport.cppm:82-113`, wire into URL construction | Bypass transport module and call rist-cpp directly from main.cpp | `source/lib/lib.cppm`, `source/transport/transport.cppm` |
| New adaptive bitrate heuristic | Modify `got_rist_statistics()` in `source/stats/stats.cppm:17-88`, update `cumulative_stats` in `source/lib/lib.cppm:43-55` (if new metrics needed) | Add bitrate logic to encode or transport modules | `source/stats/stats.cppm`, `source/lib/lib.cppm` |
| New external dependency | Add to `vcpkg.json` in `source/encode` or appropriate `CMakeLists.txt` module directory. Link via `target_link_libraries` | Add headers/links directly in main.cpp | `source/*/CMakeLists.txt`, `vcpkg.json` |
| New module/package | Create new directory under `source/`, add `.cppm` module file, add `.h`/`.cpp` pairs for CMake compatibility, create `CMakeLists.txt`, add `add_subdirectory()` to root `CMakeLists.txt:38-44`, link in executable target `CMakeLists.txt:55-68` | Add code to existing module out of scope | `source/new_module/new_module.cppm`, root `CMakeLists.txt` |

## Golden Files Per Layer

For each architectural layer, the golden file is determined by inbound import frequency: the most-imported file in a layer is the most stable and most understood.

| Layer | Golden File | Why |
|-------|-------------|-----|
| Library / Types | `source/lib/lib.cppm` | Zero inbound imports (nothing depends on it) -- it is the foundational layer that everything else depends on. Also exported via `lib.h` used by all other module `.h` headers (12 files reference `lib.h`/`lib.cppm`). Most-stable file in the codebase. |
| Encode | `source/encode/encode.cppm` | Most-imported file within the encode layer (referenced by `main.cpp:12`). Contains 36 encoder configuration methods representing the full product capability surface. The single largest source file at 578 lines. |
| Transport | `source/transport/transport.cppm` | Imported by `main.cpp:13`. The sole interface to rist-cpp `RISTNetSender`. All network data flows through `send_buffer()` in this file. |
| UI | `source/ui/ui.cppm` | Imported by `main.cpp:16` and `stats.cppm:9`. The largest file at 782 lines. Contains the complete widget hierarchy and callback binding system. Most-instructive file for understanding the application's user-facing behavior. |
| Stats | `source/stats/stats.cppm` | Imported by `main.cpp:11`. The sole file implementing the adaptive bitrate algorithm (47 lines of core logic at lines 17-63). Critical file for understanding quality-driven bitrate behavior. |
| URL Parsing | `source/url/url.h` | Only imported by `source/transport/transport.cppm:8`. Self-contained third-party library (94 lines) with no internal dependencies. The simplest layer -- one file, no module, pure header + implementation. |
| NDI Input | `source/ndi_input/ndi_input.cppm` | Imported by `main.cpp:15`. The sole file implementing NDI device discovery and preview pipeline. Self-contained GStreamer device monitoring logic. |

---

*Architecture analysis: 2026-04-28*
