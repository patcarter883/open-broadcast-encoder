# Codebase Architecture

**Analysis Date:** 2026-04-27

<guidelines>
- This document is durable intent: boundaries, layering, entrypoints, and change-routing rules.
- Do NOT write a static directory tree. It rots. Instead: reference a few canonical entrypoints and show "where changes go".
- Every layer/abstraction must include concrete file paths.
- Include "where to add new code" rules. This prevents downstream agents from scattering logic across random files.
</guidelines>

## Pattern Overview

Overall: **Strategy Pattern + Publisher-Buffer-Consumer Pipeline in a Single-Executable Desktop Application.**

Key characteristics:
- A single `open-broadcast-encoder` executable with a multi-library CMake build (`source/lib`, `source/ui`, `source/encode`, `source/transport`, `source/ndi_input`, `source/stats`, `source/url`)
- Strategy pattern on `transport_base` abstracts three protocol families (RIST, SRT, RTMP) behind a uniform send interface used by `buffer_distributor`
- GStreamer pipeline is built as a string in the `encode` class; video frames are pulled from `appsink`, converted to shared buffers, and fan-out to zero or more transport outputs
- FLTK GUI runs on the main thread; all cross-thread communication uses the `Fl::lock()/Fl::unlock()/Fl::awake()` pattern and callback function pointers
- Global mutable state lives in `main.cpp` as top-level variables (`library app`, `g_transport_manager`, `g_stats_aggregator`, `ptr_encoder`) — all components reach for these globals rather than using dependency injection

## Layers (Boundaries)

Layer: Presentation (FLTK GUI)
- Purpose: Render the user interface, collect configuration, display logs and stats
- Location: `source/ui/`
- Owns: FLTK widget hierarchy, callback wiring, log display updates, NDI device choice management, multi-output stream widget management
- Does NOT own: Business logic, encoding, transport, statistics computation
- Depends on: `source/lib/lib.h` (types), `source/transport/transport_manager.h` (multi-output)
- Used by: `source/main.cpp` (entry point, wiring), `source/ndi_input/ndi_input.h` (NDI refresh callback)
- Threading rule: All UI updates from background threads MUST use `Fl::lock()` / `Fl::unlock()` / `Fl::awake()` — never touch widgets from background threads directly

Layer: Application Entry Point (Orchestrator)
- Purpose: Initialize all subsystems, wire callbacks, manage lifecycle
- Location: `source/main.cpp`
- Owns: Global state declarations, main loop (`run_loop`), transport start/stop, preview logic, callback wiring to UI
- Does NOT own: Any business logic — it delegates to `encode`, `transport_manager`, `ndi_input`, `ui`
- Depends on: Every other layer (`source/encode/encode.h`, `source/transport/transport.h`, `source/transport/transport_manager.h`, `source/transport/buffer_distributor.h`, `source/stats/statistics_aggregator.h`, `source/lib/lib.h`, `source/ndi_input/ndi_input.h`, `source/ui/ui.h`, `source/stats/stats.h`)
- Used by: System (invoked at program startup via `main()`)

Layer: Input Discovery (NDI)
- Purpose: Discover NDI network devices and preview NDI sources
- Location: `source/ndi_input/ndi_input.h`
- Owns: GStreamer `GstDeviceMonitor` lifecycle, device enumeration, NDI preview pipeline
- Does NOT own: Configuration storage, UI rendering
- Depends on: `source/lib/lib.h` (input_config), `source/main.cpp` (log callback reference)
- Used by: `source/main.cpp` (device monitor, refresh callback), `source/ui/ui.h` (choice population)

Layer: Encoding (GStreamer Pipeline Builder)
- Purpose: Build and manage a GStreamer pipeline for video+audio encoding, expose raw video buffers
- Location: `source/encode/encode.h`
- Owns: Pipeline construction (source -> encoder -> appsink), GStreamer bus message handling, encoder parameter switching (bitrate), audio encoding
- Does NOT own: Transport/networking, statistics aggregation
- Depends on: `source/lib/lib.h` (config types, buffer_data), `source/main.cpp` (log callback, run_flag)
- Used by: `source/main.cpp` (run_loop pulls video buffers via `pull_video_buffer()`)

Layer: Transport Strategy (Protocol Abstraction)
- Purpose: Abstract RIST, SRT, and RTMP protocols behind a common send interface; manage transport lifecycle and buffer fan-out
- Location: `source/transport/`
- Owns:
  - `source/transport/transport_base.h` — abstract interface for all protocols
  - `source/transport/rist_transport.h` — RIST protocol implementation
  - `source/transport/srt_transport.h` — SRT protocol implementation
  - `source/transport/rtmp_transport.h` — RTMP protocol implementation
  - `source/transport/transport_manager.h` — factory and lifecycle manager, owns `buffer_distributor`
  - `source/transport/buffer_distributor.h` — publishes buffers to all registered transports using `shared_ptr` for zero-copy multi-consumer delivery
  - `source/transport/transport.h` — legacy single-transport wrapper (backward compatibility)
- Does NOT own: Statistics (delegates to `source/stats/`), pipeline building (delegates to `source/encode/`)
- Depends on: `source/lib/lib.h` (stream_config, buffer_data, shared_buffer, cumulative_stats), `source/url/url.h` (URL parsing in transport base)
- Used by: `source/main.cpp` (create_transport, start_all, stop_all), `source/ui/ui.h` (multi-output widget management)

Layer: Statistics
- Purpose: Aggregate per-stream statistics, compute consensus bitrate, drive adaptive encoding
- Location: `source/stats/`
- Owns:
  - `source/stats/statistics_aggregator.h` — multi-stream aggregation, consensus bitrate calculation, stale stream pruning
  - `source/stats/stats.h` — legacy RIST statistics helper (static method)
- Does NOT own: Transport internals, UI rendering
- Depends on: `source/lib/lib.h` (stream_stats, cumulative_stats), `source/stats/stats.h` (got_rist_statistics)
- Used by: `source/main.cpp` (bitrate callback), `source/encode/encode.h` (bitrate adjustment via `set_encode_bitrate`)

Layer: Configuration & Data Types
- Purpose: Define all shared types — enums, structs, configuration containers
- Location: `source/lib/lib.h`
- Owns: All enums (`input_mode`, `codec`, `encoder`, `transport_protocol`), all config structs (`input_config`, `encode_config`, `output_config`, `stream_config`), all data structs (`buffer_data`, `shared_buffer`, `cumulative_stats`, `stream_stats`, `library`)
- Does NOT own: Behavior — types are data-only
- Depends on: Nothing (this is the foundational layer)
- Used by: Every other layer in the codebase

Layer: URL Parsing (Utility)
- Purpose: Parse and manipulate URLs for transport address handling
- Location: `source/url/url.h`
- Owns: RFC 3986 compliant URL parsing (from `homer6::url` external library)
- Does NOT own: Transport logic, encoding logic
- Depends on: Nothing (pure utility)
- Used by: `source/transport/transport.h` (address parsing)

Layer: FFI Wrapper (Optional — BUILD_TAURI)
- Purpose: Expose encoder functionality via C FFI for Tauri (Rust) frontend
- Location: `source/encoder_wrapper/`
- Owns: `source/encoder_wrapper/encoder_wrapper.h`, `source/encoder_wrapper/encoder_wrapper.cpp`
- Does NOT own: Core encoding or transport logic
- Depends on: `source/encode/encode.h`
- Used by: Tauri app (external consumer), optional build target

## Entry Points

Entrypoint: main
- Location: `source/main.cpp`
- Triggers: OS process startup
- Responsibilities:
  - Initialize GStreamer (`gst_init`)
  - Declare and instantiate global state (`library app`, `g_transport_manager`, `g_stats_aggregator`)
  - Create `ndi_input` with device monitor running in background thread
  - Initialize and show FLTK UI, wire all callbacks (`init_ui_callbacks`)
  - Set up stats callback on `g_stats_aggregator` that calls `ptr_encoder->set_encode_bitrate()`
  - Enter FLTK event loop (`ui.run_ui()`)
  - On start: spawn `run_loop` thread that creates `encode`, runs encode thread, pulls buffers, distributes via `g_transport_manager.get_distributor()`
  - On stop: set `app.is_running = false`, call `g_transport_manager.stop_all()`

Entrypoint: transport_manager::create_transport
- Location: `source/transport/transport_manager.h` (implementation in `source/transport/transport_manager.cpp`)
- Triggers: Called from `source/main.cpp::run_transport()` or from UI when user adds an output stream
- Responsibilities: Dispatch to `rist_transport`, `srt_transport`, or `rtmp_transport` based on `stream_config.protocol`, add to internal registry, register with `buffer_distributor`

Entrypoint: encode::run_encode_thread
- Location: `source/encode/encode.h` (implementation in `source/encode/encode.cpp`)
- Triggers: Called from `source/main.cpp::run_loop`
- Responsibilities: Build GStreamer pipeline, start encoding, create worker threads for encoding/polling

## Data Flow (One Or Two Canonical Flows)

Flow: Video Encoding to Network Transport
1. **Input** (`source/ndi_input/ndi_input.h` or `source/encode/encode.cpp` test source) feeds raw video into a GStreamer pipeline built by `encode` (`source/encode/encode.h`)
2. **Encode** (`source/encode/encode.h`) builds a pipeline with source -> video/audio encoder -> payloader -> appsink, runs it in a background thread, and exposes `pull_video_buffer()` to fetch encoded frames as `buffer_data`
3. **Main loop** (`source/main.cpp::run_loop`) polls `encoder.pull_video_buffer()` every iteration; when `vidbuf.buf_size > 0`, converts the legacy `buffer_data` to `shared_buffer` via `g_transport_manager.get_distributor().distribute_legacy(vidbuf)`
4. **Buffer distributor** (`source/transport/buffer_distributor.h`) holds `shared_ptr<shared_buffer>` references, distributes to all registered `transport_base` instances with a mutex-protected iteration
5. **Transport implementations** (`source/transport/rist_transport.h`, `source/transport/srt_transport.h`, `source/transport/rtmp_transport.h`) each send buffers over their respective protocol, run a stats thread that reports `stream_stats`
6. **Statistics aggregator** (`source/stats/statistics_aggregator.h`) receives per-stream stats updates, computes consensus bitrate, and fires a callback that calls `ptr_encoder->set_encode_bitrate()` to close the adaptive loop

Flow: UI Configuration to Runtime
1. **User** interacts with FLTK widgets in `source/ui/ui.h` (choices, inputs, buttons)
2. **Callbacks** (wired via `init_ui_callbacks` in `source/main.cpp`) read/write `input_config`, `encode_config`, `output_config` in `library app`
3. **Transport selection** — user selects protocol from menu, UI stores via `user_data()` casting enum to `long` (`source/ui/ui.h`)
4. **Start/Stop** — `btn_start_encode` triggers `run()` (spawns `run_loop` thread); `btn_stop_encode` triggers `stop()` (sets `app.is_running = false`, stops transports)
5. **Log displays** — background threads call `ui.encode_log_append()` / `ui.transport_log_append()` which are thread-safe wrappers around `Fl::lock()/Fl::unlock()/Fl::awake()`

State management:
- All mutable shared state is global in `source/main.cpp`: `library app` (config + running flag), `g_transport_manager` (transport registry + buffer distributor), `g_stats_aggregator` (statistics + bitrate consensus), `ptr_encoder` (encoder pointer for bitrate callback), `ui` (GUI)
- `library::is_running` is `std::atomic_bool` — accessed by encode thread, main loop, and UI stop callback
- `buffer_distributor` uses `shared_ptr<shared_buffer>` for zero-copy fan-out; the underlying data buffer is copied once during `distribute_legacy()`
- Each `transport_base` implementation maintains its own `m_connected` (atomic bool) and `m_stats` (mutex-protected) state

## Key Abstractions

Abstraction: transport_base (Strategy Pattern)
- Purpose: Common interface that abstracts RIST, SRT, and RTMP transport protocols
- Examples: `source/transport/rist_transport.h`, `source/transport/srt_transport.h`, `source/transport/rtmp_transport.h`
- Pattern: Virtual abstract class with `initialize()`, `start()`, `stop()`, `send_buffer()`, `is_connected()`, `get_stream_id()`, `get_protocol()`, `get_stats()`. Each implementation manages its own threads, connections, and stats polling. `transport_manager` is the factory and registry.

Abstraction: buffer_distributor (Publisher-Subscriber with Zero-Copy)
- Purpose: Fan out a single encoded video buffer to zero or more transport outputs
- Examples: `source/transport/buffer_distributor.h`
- Pattern: Maintains a list of `shared_ptr<transport_base>`. `distribute()` takes `shared_buffer_ptr` and forwards to all. `distribute_legacy()` wraps `buffer_data` in a `shared_ptr` first. Mutex-protected iteration during distribution. Connection callback notifies UI on transport connect/disconnect.

Abstraction: statistics_aggregator (Consensus Engine)
- Purpose: Collect per-stream statistics and compute a consensus bitrate recommendation
- Examples: `source/stats/statistics_aggregator.h`
- Pattern: Maintains `vector<shared_ptr<stream_stats>>`. On each update, prunes stale streams (threshold: 10s), recalculates aggregated stats, fires callback if bitrate should change. `calculate_consensus_bitrate()` computes weighted average across streams bounded by min/max.

Abstraction: library (Global Config Container)
- Purpose: Single shared container for all application state and configuration
- Examples: `source/lib/lib.h`
- Pattern: `struct library` aggregates `input_config`, `encode_config`, `output_config`, `cumulative_stats stats`, `vector<stream_config> streams`, `atomic_bool is_running`, `vector<thread> threads`. Instantiated as global `app` in `source/main.cpp`.

## Error Handling Strategy

Strategy: Callback-based error logging with GStreamer bus message handlers.
- Encoding errors: `encode::handle_gst_message_error()` parses `GstMessage` and logs via `log_func` callback (`source/encode/encode.h`)
- Transport errors: Each transport implementation sets error state on `m_connected`; `buffer_distributor` monitors connection state and fires connection callback
- Network errors: Handled internally by each transport protocol library (RISTNetSender, SRT library, RTMP library); logged through `transport_base::log_message()` callback
- UI errors: Callbacks like `rist_log_cb` and `transport_log` in `source/main.cpp` route messages through `ui.transport_log_append()` (thread-safe)
- No exceptions thrown — all error paths use status codes (`bool` return values on `initialize()`, `start()`) and logging callbacks

## Cross-Cutting Concerns

Logging:
- Every major component accepts a log callback (function pointer or `std::function`)
- `encode` uses `log_func_ptr` (`void (*)(const std::string&)`) — `source/encode/encode.h`
- `transport_base` uses `std::function<void(const std::string&)>` — `source/transport/transport_base.h`
- RIST C library uses C callback (`int (*)(void*, enum rist_log_level, const char*)`) — `source/transport/transport.h`
- All callbacks funnel through `ui.encode_log_append()` / `ui.transport_log_append()` which handle thread safety

Threading:
- `source/main.cpp` runs encode in a background thread, device monitor in a background thread
- Each transport (`rist_transport`, `srt_transport`, `rtmp_transport`) runs its own stats thread
- FLTK UI must use `Fl::lock()` / `Fl::unlock()` / `Fl::awake()` pattern for all UI updates from background threads — `source/ui/ui.h` declares `lock()` and `unlock()` methods that wrap these calls
- Never call `ui.lock()` — always call `Fl::lock()` directly (per project convention in AGENTS.md)

Validation:
- Input validation is minimal — `stream_config` fields are strings parsed at transport initialization time
- URL parsing delegated to `homer6::url` in `source/url/url.h`
- Encoder config validated implicitly by GStreamer pipeline construction failures

## Change Routing (Where To Add New Code)

When making a change, follow these rules:

| Change type | Add/modify here | Do NOT do this | Example paths |
|---|---|---|---|
| New transport protocol (e.g., WebRTC) | Add class in `source/transport/` extending `transport_base`, register in `transport_manager::create_transport()` | Add in `main.cpp` or `encode.cpp` | `source/transport/webrtc_transport.h`, `source/transport/transport_manager.cpp` |
| New encoder backend (e.g., VA-API) | Add `pipeline_build_*` method in `source/encode/encode.h/.cpp`, add enum in `source/lib/lib.h::encoder` | Add new class outside `source/encode/` | `source/encode/encode.h` (add method), `source/lib/lib.h` (add enum) |
| New UI widget / screen | Add widget members and callback in `source/ui/ui.h`, wire in `source/ui/ui.cpp` | Add widgets in `main.cpp` | `source/ui/ui.h`, `source/ui/ui.cpp` |
| New statistics metric | Add field to `stream_stats` or `cumulative_stats` in `source/lib/lib.h`, update `statistics_aggregator` in `source/stats/` | Add stats tracking in transport implementations directly | `source/lib/lib.h`, `source/stats/statistics_aggregator.h` |
| New input source (e.g., file) | Add `input_mode` enum value in `source/lib/lib.h`, add handler in `source/ndi_input/ndi_input.h` or new `source/input/` directory | Add input logic in `source/encode/encode.cpp` | `source/lib/lib.h`, `source/ndi_input/ndi_input.h` |
| New config field | Add field to appropriate struct in `source/lib/lib.h` (`input_config`, `encode_config`, `output_config`, `stream_config`) | Add fields in multiple places or in transport/encode headers | `source/lib/lib.h` |
| New FFI function | Add declaration in `source/encoder_wrapper/encoder_wrapper.h`, implement in `source/encoder_wrapper/encoder_wrapper.cpp` | Add FFI in `main.cpp` or transport headers | `source/encoder_wrapper/encoder_wrapper.h` |
| Callback wiring for new component | Wire in `source/main.cpp` `init_ui_callbacks()` or `run_transport()` | Wire in UI class directly | `source/main.cpp` |

## Golden Files Per Layer

For each architectural layer, the most-instructive file is identified by inbound reference frequency (how many other files import/reference it):

| Layer | Golden File | Why |
|-------|-------------|-----|
| Configuration & Data Types | `source/lib/lib.h` | Referenced by every other layer — the single source of all enums, config structs, data structs, and the `library` global |
| Presentation (FLTK GUI) | `source/ui/ui.h` | Referenced by `main.cpp` for initialization and callbacks, by `ndi_input.h` for NDI refresh, by `stats.h` for UI updates |
| Application Entry Point | `source/main.cpp` | The sole top-level orchestrator — references every other header, declares all global state, wires all callbacks |
| Encoding | `source/encode/encode.h` | Referenced by `main.cpp` for pipeline creation and buffer pulling, the central pipeline builder |
| Transport Strategy | `source/transport/transport_base.h` | The abstraction point — all transport implementations inherit from it, `buffer_distributor` and `transport_manager` reference it, making it the hub of the strategy pattern |
| Buffer Distribution | `source/transport/buffer_distributor.h` | Referenced by `main.cpp` (buffer delivery), `transport_manager.h` (lifecycle), each transport implementation (connection state) |
| Statistics | `source/stats/statistics_aggregator.h` | Referenced by `main.cpp` for consensus bitrate, the aggregator of per-stream metrics |
| Statistics Helper | `source/stats/stats.h` | Referenced by `main.cpp` for legacy RIST statistics processing, small but essential bridge |
| Input Discovery | `source/ndi_input/ndi_input.h` | Referenced by `main.cpp` for device monitoring and preview, the gateway for NDI input |

---

*Architecture analysis: 2026-04-27*
