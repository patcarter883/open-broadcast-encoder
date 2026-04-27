# Codebase Concerns

**Analysis Date:** 2026-04-27

## Tech Debt

Area/component: Encoder pipeline duplication
- Issue: 12 nearly identical encoder-building methods differ only by encoder name (amfh264enc vs x264enc vs nvh264enc), codec (h264 vs h265 vs av1), and a few parameter strings. Common pattern: `name=videncoder bitrate={cbr/rc-mode} preset={quality/low-latency} ! video/x-{h264,h265,av1} ! {h264parse,av1parse}`. No abstraction or code generation reduces this.
- Files: `source/encode/encode.cpp:223-323`
- Impact: Adding a new encoder or codec variant requires editing 4+ methods and adding 4+ method declarations in the header. Bug fixes to common logic must be duplicated across all methods.
- Fix approach: Extract a single `pipeline_build_encoder(const std::string& element, const std::string& profile, const std::string& parse_element, const std::string& preset)` helper with per-encoder parameter maps. Use a lookup table or factory pattern.

Area/component: Buffer distributor shared_buffer ownership
- Issue: `distribute_legacy()` wraps a raw `uint8_t*` from GStreamer in a `shared_ptr` with a no-op destructor `[](uint8_t*){}`, meaning the actual buffer memory is never freed when the shared buffer goes out of scope. This leaks the encoded frame data each time a buffer is distributed.
- Files: `source/transport/buffer_distributor.cpp:91-99`
- Impact: Memory leak proportional to encoding throughput — for 60fps 1080p video at ~10MB/s per stream, a 4-stream setup leaks ~40MB/s.
- Fix approach: Either copy the buffer data into the shared_ptr or use a GStreamer buffer-aware custom deleter. The `distribute_legacy` method needs to either deep-copy or track the original GStreamer `GstBuffer` for proper cleanup.

Area/component: FLTK UI includes non-standard FLTK headers
- Issue: `source/ui/ui.h:6-9` includes `<FL/Fl_Help_Dialog.H>` and other widget-specific headers directly, coupling the UI to FLTK internals. The FLTK submodule is at `external/fltk` which means updates require submodule management.
- Files: `source/ui/ui.h:6-9`
- Impact: Breaking FLTK internal API changes requires UI changes. Submodule drift can cause build failures.
- Fix approach: Use forward declarations where possible and only include FLTK headers in the `.cpp`. Keep FLTK abstraction thin.

## Known Bugs

Bug: Detached preview thread — resource leak
- Symptoms: Preview threads from `preview_input()` never terminate or get joined. Thread-local GMainLoop and GStreamer elements may leak if the application exits while a preview is running.
- Files: `source/main.cpp:135-188`
- Trigger: Click "Test" input mode button, then exit the application. The detached thread and its GStreamer pipeline survive process exit or may crash on exit.
- Workaround: Use `std::async` with `detach()` replaced by storing the `std::future` or `std::thread` handle. Add a cleanup callback that joins the thread before process exit.

Bug: Raw pointer dangles — `ptr_encoder` points to local variable
- Symptoms: `ptr_encoder` is a raw pointer to a local `encode` object created inside `run_loop()`. If `run_loop` returns (e.g., early exit), `ptr_encoder` becomes a dangling pointer. Callbacks `rist_stats_cb()` at line 54-62 and the stats callback in `main()` at line 199-207 dereference `ptr_encoder` without any lifetime guarantee.
- Files: `source/main.cpp:25,67-70,54-62,199-207`
- Trigger: Stop encoding, then RIST stats callback fires or stats aggregator fires before `run_loop` returns. Accessing `ptr_encoder->set_encode_bitrate()` on a dangling pointer.
- Workaround: Replace `encode* ptr_encoder` with `std::shared_ptr<encode>` managed from the `run_loop` scope, or restructure so `encode` is a member of a longer-lived object.

Bug: GStreamer bus leak in error path of `parse_pipeline`
- Symptoms: If `gst_parse_launch` returns null after error, `this->bus` and other elements are never set, but the destructor still calls `gst_object_unref(this->bus)` on nullptr.
- Files: `source/encode/encode.cpp:369-393, source/encode/encode.cpp:21-33`
- Trigger: Invalid pipeline string causes parse failure, then `encode` destructor runs.
- Workaround: Initialize all GstElement pointers to nullptr (already done in header). Check `bus != nullptr` before unref in destructor.

Bug: NDI preview thread not joinable/cleanable
- Symptoms: `ndi_input::preview()` constructs a thread that runs a `g_main_loop_run()` blocking call. There is no mechanism to stop or join this thread — the only exit path is via GStreamer bus error/EOS message. If NDI source is disconnected, the main loop hangs indefinitely.
- Files: `source/ndi_input/ndi_input.cpp:70-127`
- Trigger: Select NDI input with no available NDI source, then call `preview()`.
- Workaround: Run main loop in a separate process or add a timeout/interrupt mechanism via a shared atomic flag checked inside the loop.

Bug: SRT and RTMP transports are stub implementations
- Symptoms: `srt_transport::start()` and `rtmp_transport::start()` create worker threads but never initialize actual sockets or connect. `send_buffer()` checks `m_socket_fd < 0` and returns failure. These transports always report disconnected.
- Files: `source/transport/srt_transport.cpp:23-42,60-93`, `source/transport/rtmp_transport.cpp:23-38,60-84`
- Trigger: Select SRT or RTMP as output protocol, configure URL, and start streaming. No data is ever sent.
- Workaround: SRT requires libsrtp linkage; RTMP typically uses GStreamer `rtmp2sink` element. Both need actual implementation work.

## Security Considerations

Area: Network addresses from configuration
- Risk: User-configurable addresses (SRT/RIST/RTMP destinations) are directly interpolated into GStreamer pipeline strings and URL parsers without validation. Malformed input could cause pipeline parsing errors or unintended behavior.
- Files: `source/lib/lib.h:129-138`, `source/main.cpp:94-115`, `source/transport/rist_transport.cpp:39`
- Current mitigation: Pipeline string construction via `std::format` with no sanitization.
- Recommendations: Validate address format (IP:port) before use. Reject addresses with special characters or path components that could inject GStreamer elements.

Area: Bitrate value from config used in integer parsing
- Risk: `std::stoi(app.encode_config.bitrate)` in `source/main.cpp:201` and `source/stats/statistics_aggregator.cpp:148` has no exception handling or bounds checking. A non-numeric or overflow value would throw `std::invalid_argument` or `std::out_of_range`.
- Files: `source/main.cpp:201`, `source/stats/statistics_aggregator.cpp:148`
- Current mitigation: None observed.
- Recommendations: Use `std::from_chars` or `try/catch` with clamping to valid bitrate range (e.g., 100-50000 kbps).

## Performance Bottlenecks

Operation: Legacy buffer distribution path
- Problem: `distribute_legacy()` converts raw GStreamer buffer pointers to `shared_ptr<shared_buffer>` by copying the raw pointer reference. The no-op deleter leaks memory. This conversion happens on every frame on the hot path.
- Files: `source/transport/buffer_distributor.cpp:91-99`
- Measurement: For 1080p60 at 10MB/s with N outputs, leaks N * 10MB/s. Conversion overhead is minimal but correctness is the primary concern.
- Suspected cause: The `distribute_legacy` was a bridge method to connect the encoder pipeline (which returns raw GStreamer buffer pointers) to the new shared buffer transport system. The ownership transfer was never completed.
- Improvement path: Either integrate GStreamer `GstMemory` refcounting into `shared_buffer` or use `gst_buffer_ref()` for proper zero-copy semantics.

Operation: Thread spin-wait in GStreamer bus loop
- Problem: `play_pipeline()` polls the bus with 1ms sleep intervals, yielding CPU in a tight loop during idle periods.
- Files: `source/encode/encode.cpp:395-409`
- Measurement: During idle encoding (test source), ~1000 sleep/yield cycles per second.
- Suspected cause: GStreamer message bus polling without proper event-driven wakeup.
- Improvement path: Use `gst_poll` or the GStreamer async message bus with proper threading to eliminate busy-wait.

## Fragile Areas

Component: Transport lifecycle management
- Files: `source/transport/transport_manager.cpp`, `source/transport/transport_manager.h`, `source/transport/buffer_distributor.cpp`
- Why fragile: Transport objects are managed through `shared_ptr` with a factory pattern. The `g_transport_manager` is a global singleton. `buffer_distributor` holds its own copy of transport pointers. If a transport is created via one path and accessed via another (e.g., legacy `run_transport()` creates a transport but `g_transport_manager.start_all()` manages it), lifecycle mismatches occur.
- Safe modification: Always create transports through `transport_manager::create_transport()` and never hold raw pointers or separate references. Add assert in `buffer_distributor` that all transports were added via `add_transport()`.
- Test coverage: Zero tests for transport lifecycle.

Component: GStreamer pipeline construction
- Files: `source/encode/encode.cpp:351-418`
- Why fragile: Pipeline string is built incrementally through string concatenation across ~30 methods. Any method that modifies `pipeline_str` without proper ordering breaks the pipeline. The `name=videncoder` naming convention is used for element retrieval (`gst_bin_get_by_name` at line 386) and fails silently if the name isn't found.
- Safe modification: Build pipeline in stages (source, processing, sink) with clear boundaries. Add pipeline validation test that asserts key elements are present after parsing.
- Test coverage: Zero pipeline tests.

Component: Global state access from callbacks
- Files: `source/main.cpp:20-37`, `source/ndi_input/ndi_input.cpp:129-133`, `source/transport/rist_transport.cpp:141-153`
- Why fragile: Multiple callbacks (`rist_stats_cb`, `encode_log`, `transport_log`) access global objects (`ui`, `ptr_encoder`, `app`) directly. No synchronization primitives protect these reads/writes. UI callback lambdas capture references to globals that may be destroyed during shutdown.
- Safe modification: Wrap all global access in `Fl::lock()/Fl::unlock()` pairs for UI objects. Use `std::shared_ptr` for `ptr_encoder` so callbacks can safely check null.
- Test coverage: Zero.

Component: NDI device monitor thread
- Files: `source/ndi_input/ndi_input.cpp:16-36`
- Why fragile: `device_monitor_thread` runs a GStreamer device monitor but does nothing with discovered devices except sleep. The `refresh_devices()` method is called from UI thread while `device_monitor` is started in a background thread — `gst_device_monitor_get_devices()` may not return fresh results without proper synchronization.
- Safe modification: Connect a GStreamer signal handler to the device monitor for push-based updates instead of polling.
- Test coverage: Zero.

Component: Encoder wrapper FFI
- Files: `source/encoder_wrapper/encoder_wrapper.cpp`, `source/encoder_wrapper/encoder_wrapper.h`
- Why fragile: 7 of 17 exported C API functions are stub implementations that return 0 without doing anything. The wrapper has its own global `wrapper_state` with a separate `library app` copy, duplicating state from the main application. Tauri calls into FFI but the preview, stats, NDI, and SDP features are incomplete.
- Safe modification: Remove stub functions or return meaningful error codes (e.g., `-2` for "not implemented"). Consolidate state so there's only one `library` instance.
- Test coverage: Zero.

## Dependency Risks

Dependency: FLTK (submodule at `external/fltk`)
- Risk: FLTK is a large, mature GUI toolkit with slow release cycles. Using a submodule means version pinning, but breaking changes in FLTK internals (used directly in `ui.h`) can cause silent breakage. The `Fl_Help_Dialog.H` and `FL/fl_callback_macros.H` includes are non-standard.
- Impact: All UI code breaks on FLTK update. Widget rendering may change without notice.
- Mitigation: Pin submodule to a specific commit. Minimize inclusion of FLTK internal headers. Consider a thin FLTK wrapper layer.

Dependency: GStreamer 1.28+
- Risk: Pipeline elements change between GStreamer versions (e.g., `decodebin3` replaced by `decodebin` or vice versa, element properties rename). Pipeline string construction has no version checking.
- Impact: Encoding pipeline may silently fail or produce incorrect output on systems with different GStreamer versions.
- Mitigation: Add runtime GStreamer version check at startup. Test pipelines against minimum supported version. Use GStreamer `gst_registry` queries to validate element availability before building pipelines.

Dependency: librist (external C library)
- Risk: `rist_transport` links against `RISTNet.h` from an external library. The stats thread in `rist_transport.cpp:133-139` is a spin loop with no actual work — stats come via callback but the thread does nothing.
- Impact: RIST transport may silently fail to send or report stale stats. Memory leaks in librist could go unnoticed.
- Mitigation: Implement the stats thread with actual work (e.g., periodic cleanup or retransmission checks). Add library version check at startup.

## Missing Critical Features

Feature gap: SRT transport not implemented
- Problem: `srt_transport.cpp` has worker and stats threads but no actual SRT socket initialization, connection, or data sending. The SRT library (libsrt) is likely not linked.
- Blocks: Users cannot stream via SRT protocol despite it being a configured option in the UI.

Feature gap: RTMP transport not implemented
- Problem: `rtmp_transport.cpp` has the same stub pattern as SRT. No socket or GStreamer pipeline for `rtmp2sink`.
- Blocks: Live streaming to YouTube/Twitch/Facebook via RTMP is unavailable.

Feature gap: SDP input parsing not implemented
- Problem: `encoder_wrapper.cpp:389` has a TODO for SDP file parsing. The main application has SDP mode in the enum but no file-based SDP parsing UI exists.
- Blocks: Users cannot load SDP session descriptions for input.

Feature gap: Encoder wrapper preview API not implemented
- Problem: `encoder_start_preview()` and `encoder_stop_preview()` in the C FFI are stubs. Tauri frontend calls these but nothing happens.
- Blocks: Frontend preview feature is non-functional.

## Test Coverage Gaps

Untested area: Transport protocol implementations
- What's not tested: RIST send/receive lifecycle, SRT stub behavior, RTMP stub behavior, buffer distribution to multiple transports, transport creation/removal via `transport_manager`.
- Files: `source/transport/rist_transport.cpp`, `source/transport/srt_transport.cpp`, `source/transport/rtmp_transport.cpp`, `source/transport/buffer_distributor.cpp`, `source/transport/transport_manager.cpp`
- Risk: Any change to transport code can silently break streaming without detection. Connection state transitions, buffer ownership, and thread lifecycle are particularly fragile.
- Priority: High

Untested area: GStreamer pipeline construction
- What's not tested: Pipeline building for all 12 encoder combinations (4 encoders x 3 codecs), pipeline parsing failures, element name resolution, buffer extraction (`pull_video_buffer`, `pull_audio_buffer`).
- Files: `source/encode/encode.cpp`, `source/encode/encode.h`
- Risk: Adding a new encoder codec adds 4 more methods to test. Pipeline string errors are caught only at runtime.
- Priority: High

Untested area: Statistics aggregation and bitrate adjustment
- What's not tested: `calculate_consensus_bitrate()` weighted average, `should_adjust_bitrate()` threshold logic, `prune_stale_streams()`, multi-stream stats merging, `get_average_quality()`.
- Files: `source/stats/statistics_aggregator.cpp`
- Risk: Bitrate adjustment algorithm could degrade video quality under load without detection. Edge cases (no connected streams, all streams disconnected, single stream) are unverified.
- Priority: Medium

Untested area: Encoder wrapper FFI
- What's not tested: C ABI compatibility, state initialization/destruction lifecycle, config setting through C struct, error paths for null pointer inputs.
- Files: `source/encoder_wrapper/encoder_wrapper.cpp`, `source/encoder_wrapper/encoder_wrapper.h`
- Risk: Breaking the Tauri frontend integration without knowing. Many functions are stubs and return success without doing anything.
- Priority: Medium

Untested area: Buffer ownership and lifetime
- What's not tested: `distribute_legacy()` shared buffer lifetime, GStreamer buffer mapping (`pull_video_buffer`, `pull_audio_buffer`), concurrent access to buffers from encoder thread to transport threads.
- Files: `source/transport/buffer_distributor.cpp`, `source/encode/encode.cpp:482-512`
- Risk: Memory leaks, use-after-free, data corruption under high load.
- Priority: High

## Downstream Impact Ranking

| Rank | Concern | Blocks | Severity | Fix effort |
|------|---------|--------|----------|------------|
| 1 | Raw pointer `encode* ptr_encoder` dangling / global state | Change routing: transport config, UI logging, stats display, bitrate adjustment, error handling | critical | small |
| 2 | GStreamer pipeline duplication (12 methods) | Change routing: new encoder support, codec changes, encoder parameter tuning, pipeline validation | critical | medium |
| 3 | Buffer distributor memory leak (`distribute_legacy`) | Change routing: streaming output, multi-stream distribution, buffer ownership, transport integration | moderate | medium |
| 4 | Test coverage gaps (0 meaningful tests) | Change routing: any feature addition, bug fix, refactor, API change, performance tuning | moderate | large |
| 5 | SRT/RTMP stub transports | Change routing: SRT output config, RTMP output config, transport lifecycle management | moderate | large |
| 6 | Detached preview threads | Change routing: input preview, test source, resource cleanup on exit | minor | small |

Ranking heuristic: Concern 1 ranks highest because it blocks 5 change-routing rows and introduces undefined behavior on every stats callback invocation. Concern 2 blocks new encoder/codec integration which is the core feature expansion path. Concern 3 blocks all streaming output changes — a memory leak that grows with usage is harder to justify than a missing feature. Concern 4 blocks all other fixes because no test safety net exists.

---

*Concerns audit: 2026-04-27*
