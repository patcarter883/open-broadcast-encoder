# Codebase Architecture

**Analysis Date:** 2026-05-02

## Pattern Overview

Overall: **Layered Modular Monolith** with callback-based component communication and a single global state object (`library app`).

Key characteristics:
- Single executable entry point (`source/main.cpp`) orchestrating all components
- Shared global state via `library` struct (`source/lib/lib.h:76`) passed by reference to all layers
- Component communication via function pointer callbacks (no pub/sub, no events, no signals)
- GStreamer pipeline built as a pipeline string (`source/encode/encode.cpp:339`), parsed and played at runtime
- Hardware encoder selection via double-dispatch: encoder vendor -> codec combination (4 x 3 = 12 encoder implementations)
- Adaptive bitrate controlled by RIST statistics feedback loop (`source/stats/stats.cpp:6`)
- FLTK UI runs in main thread; background threads use `Fl::lock()`/`Fl::unlock()` for UI updates (`source/ui/ui.cpp:467`)

## Layers (Boundaries)

### Layer: Core Types (lib)
- **Purpose:** Shared types, config structs, enums, and global state container that every layer depends on
- **Location:** `source/lib/`
- **Owns:** `input_mode`, `codec`, `encoder` enums; `input_config`, `encode_config`, `output_config`, `cumulative_stats`, `buffer_data` structs; `library` global state container
- **Does NOT own:** Any business logic, UI code, encoding logic, transport logic
- **Depends on:** Nothing (pure header-only types + trivial constructor in `source/lib/lib.cpp:1`)
- **Used by:** Every other layer. Import chain: `source/encode/encode.h:16` -> `source/lib/lib.h`, `source/ui/ui.h:19` -> `source/lib/lib.h`, `source/transport/transport.h:10` -> `source/lib/lib.h`, `source/ndi_input/ndi_input.h:9` -> `source/lib/lib.h`, `source/stats/stats.h:3` -> `source/lib/lib.h`

### Layer: Encoding Pipeline (encode)
- **Purpose:** GStreamer pipeline construction, parsing, playback, and buffer extraction
- **Location:** `source/encode/`
- **Owns:** Pipeline string building (`source/encode/encode.cpp:35`-`349`), GStreamer element management, encoder-specific GStreamer element strings (`source/encode/encode.cpp:211`-`311`), video/audio buffer pulling (`source/encode/encode.cpp:464`-`494`), bitrate adjustment (`source/encode/encode.cpp:496`), GStreamer bus message handling (`source/encode/encode.cpp:428`-`462`)
- **Does NOT own:** Config definitions (in lib), transport logic, UI updates, input source management
- **Depends on:** `source/lib/lib.h` (types), GStreamer libraries (via CMake)
- **Used by:** `source/main.cpp` (instantiated in `run_loop`, line 60), `source/stats/stats.cpp` (bitrate adjustment via `set_encode_bitrate`)

### Layer: Transport (transport)
- **Purpose:** RIST network transport, wrapping rist-cpp/RISTNet for sending encoded video buffers
- **Location:** `source/transport/`
- **Owns:** RIST sender setup (`source/transport/transport.cpp:39`), buffer sending (`source/transport/transport.cpp:72`), URL construction for RIST endpoints (`source/transport/transport.cpp:45`-`63`), callback forwarding for log/stats (`source/transport/transport.cpp:22`-`37`)
- **Does NOT own:** Statistics processing, UI updates, config definitions
- **Depends on:** `source/lib/lib.h` (types), `source/url/url.h` (URL parsing), `rist-cpp` / `RISTNet.h`
- **Used by:** `source/main.cpp` (line 85-91), `source/encode/encode.cpp` (indirectly via main's buffer push loop, line 70)

### Layer: UI (ui)
- **Purpose:** FLTK desktop application providing configuration controls, stats display, and log viewers
- **Location:** `source/ui/`
- **Owns:** FLTK widget hierarchy (`source/ui/ui.cpp:141`-`429`), menu definitions for input/codec/encoder selection (`source/ui/ui.cpp:19`-`139`), callback bindings (`source/ui/ui.cpp:618`-`695`), thread-safe log appending (`source/ui/ui.cpp:443`-`459`), lock/unlock helpers (`source/ui/ui.cpp:467`-`476`), start/stop button management (`source/ui/ui.cpp:590`-`606`)
- **Does NOT own:** Business logic, encoding logic, transport logic, statistics computation
- **Depends on:** `source/lib/lib.h` (types), FLTK (via CMake)
- **Used by:** `source/main.cpp` (global `ui` instance, line 20), `source/stats/stats.cpp` (UI updates, lines 56-75)

### Layer: NDI Input (ndi_input)
- **Purpose:** NDI device discovery via GStreamer device monitor and NDI preview capability
- **Location:** `source/ndi_input/`
- **Owns:** GStreamer device monitor setup (`source/ndi_input/ndi_input.cpp:16`-`37`), device name refresh (`source/ndi_input/ndi_input.cpp:39`-`52`), NDI preview pipeline creation (`source/ndi_input/ndi_input.cpp:59`-`115`)
- **Does NOT own:** Config definitions, encoding logic, UI widget creation
- **Depends on:** `source/lib/lib.h` (types), GStreamer libraries
- **Used by:** `source/main.cpp` (global `ndi` instance, line 33), `source/ui/ui.cpp` (via callback `refresh_ndi_devices` at line 362)

### Layer: Statistics & Adaptive Bitrate (stats)
- **Purpose:** RIST statistics processing with adaptive bitrate control
- **Location:** `source/stats/`
- **Owns:** Quality-based bitrate adjustment algorithm (`source/stats/stats.cpp:8`-`36`), cumulative statistics tracking with online averages (`source/stats/stats.cpp:38`-`54`), UI stats display updates (`source/stats/stats.cpp:56`-`75`), return value indicating bitrate changed
- **Does NOT own:** RIST data collection (done by rist-cpp), config definitions, UI widget creation
- **Depends on:** `source/lib/lib.h` (types), `source/ui/ui.h` (UI updates), rist-cpp (`RISTNet.h`)
- **Used by:** `source/main.cpp` (callback `rist_stats_cb`, line 50-55)

### Layer: URL Parsing (url)
- **Purpose:** RFC 3986-compliant URL parsing for RIST endpoint configuration
- **Location:** `source/url/`
- **Owns:** URL string parsing (`source/url/url.cc`), host/port/path/query extraction (`source/url/url.h:34`-`44`)
- **Does NOT own:** Any business logic
- **Depends on:** Nothing (pure C++ implementation)
- **Used by:** `source/transport/transport.cpp:6` (imported for RIST URL construction)

### Layer: External Dependencies
- **Location:** `external/`
- **Owns:** Vendored/third-party code
- **Contents:**
  - `external/fltk/` - FLTK GUI toolkit (git submodule)
  - `external/rist-cpp/` - RIST protocol C++ wrapper (git submodule)
  - `external/ndi-stub/` - NDI stub interface
  - `external/sdp-tools-cpp/` - SDP parsing tools (commented out in CMakeLists.txt, not active)

## Entry Points

### Entrypoint: main
- **Location:** `source/main.cpp`
- **Triggers:** OS process launch
- **Responsibilities:**
  - GStreamer initialization (`source/main.cpp:115`)
  - NDI device monitoring startup (`source/main.cpp:117`)
  - UI initialization and callback wiring (`source/main.cpp:118`-`126`)
  - FLTK event loop entry (`source/main.cpp:128`)
  - Global state setup: `library app` at `source/main.cpp:18`, global `ui` at `source/main.cpp:20`, global `ndi` at `source/main.cpp:33`
  - Callback registration: `rist_log_cb` at `source/main.cpp:42`, `rist_stats_cb` at `source/main.cpp:50`
  - Transport thread spawning via `run()` at `source/main.cpp:75`
  - Encode + transport loop in `run_loop()` at `source/main.cpp:57`

## Data Flow (One Canonical Flow)

### Flow: Video Encode -> Transport -> Network

1. **Input Source** -> GStreamer demuxer via pipeline string built in `source/encode/encode.cpp:35`-`73` (SDP/RTP, NDI, or MPEGTS)
2. **Decode** -> `videoconvert`/`audioconvert` elements in `source/encode/encode.cpp:83`-`115`
3. **Encode** -> Encoder element selected by vendor+codec in `source/encode/encode.cpp:122`-`311` (AMD/NVENC/QSV/Software x H264/H265/AV1)
4. **Mux** -> `mpegtsmux` element in `source/encode/encode.cpp:77`-`81`
5. **Buffer Pull** -> `encode::pull_video_buffer()` in `source/encode/encode.cpp:464` reads from `appsink` element
6. **Transport Send** -> `transporter->send_buffer(vidbuf, 0)` in `source/main.cpp:70` sends via RIST
7. **RIST Network** -> `rist_sender->sendData()` in `source/transport/transport.cpp:74` transmits over network
8. **Stats Feedback** -> RIST statistics callback `rist_stats_cb` at `source/main.cpp:50` triggers adaptive bitrate

### Flow: Configuration -> UI -> Runtime

1. User selects input protocol in `source/ui/ui.cpp:483`-`535` (SDP/NDI/MPEGTS)
2. User selects codec + encoder in `source/ui/ui.cpp:575`-`588`
3. UI callbacks update config structs in-place (`source/main.cpp:119`-`126` pass pointers)
4. "Start Encode" button triggers `run()` -> `run_loop()` in `source/main.cpp:57`
5. New `encode` instance created with current config at `source/main.cpp:60`
6. Pipeline built with selected config at `source/encode/encode.cpp:339`-`349`

### State Management

- **Global state:** `library app` at `source/main.cpp:18` holds `input_config`, `encode_config`, `output_config`, `cumulative_stats`, and `is_running` atomic flag
- **Config mutation:** UI callbacks modify config structs in-place via raw pointers passed from `source/main.cpp:119`-`126`
- **Running flag:** `std::atomic_bool is_running` in `library` struct at `source/lib/lib.h:87`, shared between encode loop and stop logic
- **Stats accumulation:** `cumulative_stats` at `source/lib/lib.h:40` grows vectors unbounded (memory leak risk - vectors never cleared)
- **Encoder pointer:** Raw pointer `encode* ptr_encoder` at `source/main.cpp:21` for bitrate callback access (dangling reference risk on encoder destruction)

## Key Abstractions

### Abstraction: Config Structs
- **Purpose:** Plain data structs holding user configuration, passed by reference to all components
- **Examples:** `source/lib/lib.h:54` (`input_config`), `source/lib/lib.h:59` (`encode_config`), `source/lib/lib.h:65` (`output_config`)
- **Pattern:** POPOs (Plain Old Data Objects) with default values, mutated by UI callbacks

### Abstraction: Callback Pattern
- **Purpose:** Cross-component communication via function pointers and `std::function`
- **Examples:** `source/main.cpp:23` (`encode_log`), `source/main.cpp:28` (`transport_log`), `source/main.cpp:50` (`rist_stats_cb`), `source/encode/encode.h:30` (`log_func`)
- **Pattern:** Lambda closures capturing component references, registered as callbacks on construction

### Abstraction: GStreamer Pipeline String
- **Purpose:** Pipeline constructed as format string, parsed at runtime by GStreamer
- **Examples:** `source/encode/encode.cpp:53`-`73` (source), `source/encode/encode.cpp:213`-`311` (encoder elements)
- **Pattern:** `std::format`-based string building with switch-case dispatch for encoder vendor + codec combinations

### Abstraction: RIST Statistics Feedback Loop
- **Purpose:** Closed-loop adaptive bitrate based on network quality
- **Examples:** `source/stats/stats.cpp:8`-`36` (algorithm), `source/main.cpp:50`-`55` (callback wiring)
- **Pattern:** RIST stats callback -> quality analysis -> bitrate delta calculation -> encoder property update -> stats update -> UI refresh

### Abstraction: FLTK Widget Menu Arrays
- **Purpose:** Static menu item arrays with `user_data` encoding enum values for type-safe selection
- **Examples:** `source/ui/ui.cpp:19` (`menu_choice_input_protocol`), `source/ui/ui.cpp:72` (`menu_choice_codec`), `source/ui/ui.cpp:102` (`menu_choice_encoder`)
- **Pattern:** `Fl_Menu_Item` arrays with `user_data_` set to cast enum values, retrieved via `mvalue()->user_data()` + reinterpret cast

## Error Handling Strategy

Strategy: **GStreamer Bus Message Callbacks + Logging**

- **GStreamer errors:** Handled via `handle_gstreamer_message()` at `source/encode/encode.cpp:450`-`462`, dispatched to `handle_gst_message_error()` at `source/encode/encode.cpp:428`-`442` or `handle_gst_message_eos()` at `source/encode/encode.cpp:444`-`448`
- **Error logging:** All errors logged through `log_func` callback chain -> `ui.encode_log_display` or `ui.transport_log_display`
- **Pipeline parse errors:** Caught at `source/encode/encode.cpp:359`-`362`, logged but pipeline continues in degraded state
- **RIST errors:** Logged through `rist_log_cb` at `source/main.cpp:42`-`48` -> `ui.transport_log_display`
- **No exception throwing:** GStreamer APIs use error pointers (`GError*`), not exceptions. C++ code uses early returns and boolean flags rather than exceptions.
- **Examples:** `source/encode/encode.cpp:360` (parse error), `source/ndi_input/ndi_input.cpp:86`-`94` (preview error)

## Cross-Cutting Concerns

### Logging
- **Approach:** Function pointer callbacks from components -> UI text displays
- **Implementation:** Each component holds a `log_func` (function pointer or `std::function`), called with string messages
- **Flow:** Component log -> `main.cpp` lambda -> `ui.log_append()` -> `Fl_Text_Display::insert()`
- **Examples:** `source/encode/encode.cpp:501` (`encode::log`), `source/ndi_input/ndi_input.cpp:117` (`ndi_input::log`), `source/main.cpp:23`-`26` (encode_log lambda)

### Threading
- **Approach:** FLTK single-threaded main loop; background threads for encoding and device monitoring
- **UI locking:** All cross-thread UI updates wrapped in `Fl::lock()` / `Fl::unlock()` / `Fl::awake()`
- **Examples:** `source/ui/ui.cpp:467`-`476` (`lock`/`unlock` helpers), `source/stats/stats.cpp:56`-`75` (stats UI update with lock), `source/ndi_input/ndi_input.cpp:32`-`35` (device monitor thread loop)

### Configuration
- **Approach:** Global `library` struct holds all config + state, passed by reference
- **Mutability:** UI callbacks mutate config structs in-place; runtime changes take effect immediately
- **Examples:** `source/lib/lib.h:76`-`99` (`library` struct), `source/ui/ui.cpp:618`-`695` (`init_ui_callbacks` passes config pointers)

## Change Routing (Where To Add New Code)

When making a change, follow these rules:

| Change type | Add/modify here | Do NOT do this | Example paths |
|---|---|---|---|
| New input source (e.g. RTSP) | `source/encode/encode.cpp:35`-`73` (add case to `pipeline_build_source`), `source/lib/lib.h:9` (`input_mode` enum), `source/ui/ui.cpp:19` (menu item) | Add input logic to encode.cpp outside of `pipeline_build_source` | `source/encode/encode.cpp:51`, `source/lib/lib.h:11` |
| New codec (e.g. VP9) | `source/lib/lib.h:17` (`codec` enum), `source/encode/encode.cpp:194`-`311` (add encoder case for each vendor), `source/ui/ui.cpp:72` (menu item) | Add codec string directly in encoder methods without enum | `source/encode/encode.cpp:196`, `source/lib/lib.h:21` |
| New hardware encoder (e.g. VideoToolbox on macOS) | `source/lib/lib.h:24` (`encoder` enum), `source/encode/encode.cpp:122`-`209` (add dispatch case), implement `pipeline_build_vt_h264_encoder()` etc. | Add encoder logic to software encoder method | `source/encode/encode.cpp:124`, `source/lib/lib.h:28` |
| New UI control/widget | `source/ui/ui.h:23`-`100` (add member + method declarations), `source/ui/ui.cpp:141`-`429` (widget construction), `source/ui/ui.cpp:618`-`695` (callback binding) | Add widgets directly in main.cpp | `source/ui/ui.cpp:157`, `source/ui/ui.h:31` |
| New transport protocol | `source/transport/transport.h:14`-`29` (add class or method), `source/transport/transport.cpp:39`-`75` (implementation), `source/main.cpp:85`-`91` (setup wiring) | Add transport logic in encode.cpp | `source/transport/transport.cpp:39`, `source/main.cpp:87` |
| New stats metric | `source/lib/lib.h:40`-`52` (`cumulative_stats` struct), `source/stats/stats.cpp:38`-`75` (tracking + UI update) | Track stats in transport layer directly | `source/lib/lib.h:43`, `source/stats/stats.cpp:38` |
| New bitrate adaptation rule | `source/stats/stats.cpp:8`-`36` (algorithm body) | Add adaptation logic in encode.cpp or transport.cpp | `source/stats/stats.cpp:14`, `source/stats/stats.cpp:22` |
| New URL parsing feature | `source/url/url.h:27`-`90`, `source/url/url.cc` | Add URL parsing in transport layer | `source/url/url.h:37`, `source/url/url.cc` |
| Adding a new module/package | Create `source/<module>/` with `CMakeLists.txt`, `<module>.h`, `<module>.cpp`; add `add_subdirectory` in root `CMakeLists.txt`; link in `target_link_libraries` for `open-broadcast-encoder_exe` | Add files outside `source/` without CMake wiring | `source/stats/CMakeLists.txt` (pattern example) |

## Golden Files Per Layer

Golden files identified by inbound import frequency (most-imported = most stable + most-understood):

| Layer | Golden File | Why |
|-------|-------------|-----|
| Core Types (lib) | `source/lib/lib.h` | Imported by 5/6 layers: encode (`source/encode/encode.h:16`), ui (`source/ui/ui.h:19`), transport (`source/transport/transport.h:10`), ndi_input (`source/ndi_input/ndi_input.h:9`), stats (`source/stats/stats.h:3`). Defines all shared types. |
| Encoding Pipeline | `source/encode/encode.h` | Highest complexity file in encode layer with 40+ private methods defining the full encoder dispatch tree. Referenced by main.cpp and stats module. |
| Transport | `source/transport/transport.cpp` | Only source file in transport layer (no separate .h implementation split). Implements all transport logic in 75 lines. Single entry point for RIST functionality. |
| UI | `source/ui/ui.cpp` | Largest single file (695 lines). Contains all FLTK widget definitions, menu arrays, callbacks, and UI state management. Most complex file in the project. |
| NDI Input | `source/ndi_input/ndi_input.h` | Small header-only interface (33 lines). Clean dependency on `source/lib/lib.h`. Defines the NDI interface consumed by main.cpp and ui. |
| Statistics | `source/stats/stats.cpp` | Single static method implementation (78 lines). Critical adaptive bitrate algorithm. Only stats implementation file. |
| URL Parsing | `source/url/url.h` | Third-party library header (94 lines). Stable API surface. Used exclusively by transport for RIST URL construction. |
| Entry Point | `source/main.cpp` | Orchestrates all layers. Defines global state and callback wiring. Only file that directly instantiates and connects all components. |

---

*Architecture analysis: 2026-05-02*
