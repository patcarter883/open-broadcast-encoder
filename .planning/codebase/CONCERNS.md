# Codebase Concerns

**Analysis Date:** 2026-04-27

## Tech Debt

Area/component: FLTK log appends
- Issue: `transport_log_append` and `encode_log_append` in `ui.cpp` call `insert()` on FLTK text displays WITHOUT `Fl::lock()`/`Fl::unlock()`/`Fl::awake()`. The locking calls exist in the code but are commented out (`ui.cpp:468-471`, `ui.cpp:474-477`).
- Files: `[source/ui/ui.cpp:466-477]`
- Impact: Race condition when background threads (encoder pipeline, RIST stats callbacks, transport stats) append log text simultaneously with FLTK's UI redraw. Results in corrupted text buffer, crashes, or deadlocks.
- Fix approach: Restore the `Fl::lock()` / `Fl::unlock()` / `Fl::awake()` pattern around all `insert()` calls as documented in AGENTS.md.

Area/component: SDP pipeline input
- Issue: `pipeline_build_source()` in `encode.cpp:38-64` uses a hardcoded SDP string for the SDP input mode instead of reading the user-provided SDP content from `input_c.selected_input`.
- Files: `[source/encode/encode.cpp:38-64]`
- Impact: SDP input mode does not accept user-provided SDP data; the "Open SDP File" button in `ui.cpp:193` has no wired handler. SDP input is completely non-functional.
- Fix approach: Read SDP file content into `input_c.selected_input`, pass it to `std::format` in `pipeline_build_source()`.

Area/component: Encoder lifecycle management
- Issue: The `encode` class destructor joins threads and unrefs GStreamer elements (`encode.cpp:21-33`), but there is no explicit cleanup triggered when the app exits or when the encoder is stopped. The `stop_encode_thread()` function has dead code with commented-out `std::future` pattern (`encode.cpp:420-443`).
- Files: `[source/encode/encode.cpp:420-443]`
- Impact: Encoder may not cleanly stop if `is_running` flag is not properly coordinated with GStreamer pipeline state. Stale GStreamer elements can leak.
- Fix approach: Call `stop_encode_thread()` from the main cleanup path, remove dead code, add timeout to thread join.

Area/component: `lib.cpp` constructor
- Issue: The `library()` constructor in `lib.cpp:3-7` wraps initialization in a try/catch that calls `std::terminate()`, but the actual constructor body only initializes `is_running`. The `threads` vector and all other members are default-initialized by value initializers.
- Files: `[source/lib/lib.cpp:3-7]`
- Impact: Unnecessary exception handling complexity. If initialization throws (unlikely for atomic init), the process terminates abruptly with no logging.
- Fix approach: Simplify to direct initialization, remove try/catch.

## Known Bugs

Bug: UI log appends are not thread-safe
- Symptoms: Intermittent crashes, corrupted log text, or frozen UI during active encoding/transmission
- Files: `[source/ui/ui.cpp:466-477]`
- Trigger: Start encoding with RIST transport, observe log displays under active video stream
- Workaround: None currently; all log appends bypass threading primitives

Bug: Detached threads cannot be monitored or joined
- Symptoms: Preview threads run after app teardown causes undefined behavior. No way to wait for preview completion or detect preview errors.
- Files: `[source/main.cpp:188]`, `[source/ndi_input/ndi_input.cpp:72]`
- Trigger: Click "Preview Input" on test pattern or NDI source, then close application or change input before preview completes
- Workaround: Wait for preview pipeline to finish (via EOS or error message) before changing state

Bug: `parse_pipeline` silently continues on GStreamer parse failure
- Symptoms: Pipeline created but `datasrc_pipeline` remains nullptr, subsequent calls to `gst_element_set_state` or `gst_bin_get_by_name` crash on null pointer
- Files: `[source/encode/encode.cpp:376-383]`
- Trigger: Any malformed pipeline string (invalid element name, wrong caps, missing plugin)
- Workaround: None; GStreamer plugin availability must be verified at runtime before pipeline build

Bug: SDP input does not use user-provided SDP
- Symptoms: SDP input mode always uses a hardcoded SDP from `encode.cpp:38-49` rather than user input
- Files: `[source/encode/encode.cpp:58-63]`, `[source/ui/ui.cpp:193]`
- Trigger: Select SDP/RTP input protocol, click "Open SDP File"
- Workaround: Use test pattern mode or hardcoded SDP for testing

## Security Considerations

Area: Network address input
- Risk: RIST/SRT transport addresses (`rist_transport.cpp:39-53`, `srt_transport.cpp:36`) are formatted into URL strings without validation. Malformed addresses could cause buffer issues or injection into external commands.
- Files: `[source/transport/rist_transport.cpp:39-53]`, `[source/transport/srt_transport.cpp:36]`, `[source/transport/transport.cpp:45-60]`
- Current mitigation: None
- Recommendations: Validate IP address format and port range (0-65535) before URL construction

Area: Bitrate input validation
- Risk: `main.cpp:201` calls `std::stoi(app.encode_config.bitrate)` without validation or exception handling. Non-numeric input causes `std::invalid_argument` exception, crashing the app.
- Files: `[source/main.cpp:201]`, `[source/stats/stats.cpp:11]`, `[source/stats/statistics_aggregator.cpp:148]`
- Current mitigation: None
- Recommendations: Validate numeric range before parsing, catch exceptions, use `std::from_chars` for safer conversion

Area: Buffer distribution raw pointer exposure
- Risk: `distribute_legacy()` in `buffer_distributor.cpp:93-98` creates a `shared_buffer` with a no-op deleter (`[](uint8_t*) {}`), meaning the shared buffer data outlives the original `buffer_data` and can access freed memory from the encoder's GStreamer buffer.
- Files: `[source/transport/buffer_distributor.cpp:93-98]`
- Current mitigation: None; relies on transport sending data faster than encoder frees buffers
- Recommendations: Copy buffer data into the `shared_buffer` or use GStreamer's native reference counting

## Performance Bottlenecks

Operation: Encoder message polling loop
- Problem: `play_pipeline()` in `encode.cpp:399-408` uses a 1ms sleep interval with thread yield. At 30fps video, this polls ~1000x/sec for GStreamer messages, consuming ~1-2% CPU per core.
- Files: `[source/encode/encode.cpp:398-408]`
- Measurement: Sleep is 1ms, loop runs ~1000 times/sec
- Suspected cause: GStreamer bus has a blocking pop available but code uses timed pop with 1ms granularity for power efficiency
- Improvement path: Use blocking `gst_bus_pop()` with a condition variable or GStreamer's async message handling

Operation: RIST stats polling thread
- Problem: `rist_transport.cpp:134-138` runs a 100ms polling loop that does nothing (stats come via callback). This is dead work consuming ~0.1% CPU per transport.
- Files: `[source/transport/rist_transport.cpp:134-138]`
- Measurement: 10ms sleep per iteration, loop runs ~100 times/sec
- Improvement path: Remove the polling thread entirely; rely solely on RIST callback

Operation: Statistics accumulation in `stats.cpp`
- Problem: `got_rist_statistics()` in `stats.cpp:38-52` uses `std::accumulate` with linear iteration over growing vectors for every stats update (~10-100ms interval). Bandwidth/encode_bitrate vectors grow unbounded.
- Files: `[source/stats/stats.cpp:38-52]`
- Measurement: Each stats update requires O(n) vector iteration where n grows unbounded
- Suspected cause: Running averages computed from scratch each time instead of incrementally
- Improvement path: Maintain running sums, cap vectors to last N samples, use Welford's algorithm for running averages

## Fragile Areas

Component: Transport protocol factory
- Files: `[source/transport/transport_manager.cpp:13-37]`
- Why fragile: SRT and RTMP transport implementations are stubs. `srt_transport.cpp:14-42` and `rtmp_transport.cpp:14-38` have no actual socket initialization or library calls. `start()` always returns true regardless of whether the underlying transport is ready.
- Safe modification: Verify SRT/RTMP library availability at startup, add readiness checks in `start()`, implement actual send/recv logic
- Test coverage: No tests for transport factory. `srt_transport` and `rtmp_transport` have no tests at all.

Component: Buffer distributor concurrency
- Files: `[source/transport/buffer_distributor.cpp:76-98]`
- Why fragile: `distribute()` holds the mutex while calling `send_buffer()` on each transport sequentially. If any transport's `send_buffer()` blocks (network timeout, backpressure), all other transports are blocked. The `distribute_legacy()` path also allocates a new `shared_buffer` under the lock, adding allocation latency.
- Safe modification: Pre-allocate shared buffers, consider async send, or use lock-free queue pattern
- Test coverage: No tests for buffer distributor

Component: GStreamer pipeline element lifecycle
- Files: `[source/encode/encode.cpp:369-393]`, `[source/encode/encode.cpp:482-512]`
- Why fragile: `parse_pipeline()` gets elements by name but doesn't check for null returns from `gst_bin_get_by_name()`. `pull_video_buffer()` calls `gst_buffer_map()` without a corresponding `gst_buffer_unmap()`. Both are memory/resource leaks.
- Safe modification: Null-check all `gst_bin_get_by_name()` results. Add `gst_buffer_unmap()` after use in `pull_video_buffer()` and `pull_audio_buffer()`.
- Test coverage: No pipeline build or buffer lifecycle tests

## Dependency Risks

Dependency: FLTK (via git submodule)
- Risk: FLTK is pulled as a git submodule from the main repo (`external/fltk`). The submodule path `external/fltk` is included as `SYSTEM` in CMake (`CMakeLists.txt:25`), which suppresses warnings from FLTK headers.
- Files: `[.gitmodules:4-6]`, `[CMakeLists.txt:25]`
- Impact: If FLTK version diverges or build changes, the main project may silently break. System include path means clang-tidy warnings from FLTK are suppressed but FLTK issues are invisible.
- Mitigation: Pin submodule to specific commit, consider vendored copy with explicit version

Dependency: RIST C++ wrapper (via git submodule)
- Risk: Custom fork at `https://github.com/patcarter883/rist-cpp.git` wraps the RIST library. The RIST C library itself may have unmaintained dependencies.
- Files: `[.gitmodules:7-9]`, `[source/transport/rist_transport.cpp]`, `[source/transport/transport.cpp]`
- Impact: RIST is the only fully implemented transport. Failure of RIST library means loss of all streaming capability.
- Mitigation: Monitor ristrist-cpp upstream for updates, consider depending directly on C RIST library

Dependency: GStreamer 1.28+
- Risk: CMake requires GStreamer >=1.28 (`CMakeLists.txt:31-35`). This pins the minimum distro version and may break on older systems. GStreamer 1.28 was released in 2023, but older enterprise distros may ship 1.22-1.24.
- Files: `[CMakeLists.txt:31-35]`
- Impact: Cannot build on Ubuntu 22.04 (ships GStreamer 1.20), RHEL 8/9 without COPR repos
- Mitigation: Document supported distros, consider lower minimum version with feature detection

## Missing Critical Features (If Any)

Feature gap: SDP file input
- Problem: User can select SDP input mode and click "Open SDP File" but there is no file dialog handler wired up. The button callback is not registered in `ui.cpp` and `pipeline_build_source()` ignores `input_c.selected_input` for SDP mode.
- Blocks: Any SDP-based input workflows (IP camera feeds, RTP streams with SDP description)

Feature gap: SRT and RTMP transport implementation
- Problem: `srt_transport.cpp` and `rtmp_transport.cpp` are stubs with no actual protocol implementation. `send_buffer()` contains only comments indicating where SRT/RTMP calls would go.
- Blocks: Multi-protocol streaming, SRT/RTMP output to YouTube/Facebook/Twitch, any workflow requiring non-RIST transport

Feature gap: NDI preview cleanup
- Problem: `ndi_input.cpp:70-127` creates a GStreamer pipeline for preview with `autovideosink` but does not handle cleanup if the user closes the window or changes input while preview is running. `gst_device_monitor` in `run_device_monitor()` has no shutdown mechanism other than `stop_device_monitor()`.
- Blocks: Reliable NDI preview in production environments

## Test Coverage Gaps

Untested area: Transport implementations
- What's not tested: `rist_transport.cpp`, `srt_transport.cpp`, `rtmp_transport.cpp` start/stop/send lifecycle, `buffer_distributor.cpp` distribution logic, `transport_manager.cpp` factory pattern
- Files: `[source/transport/rist_transport.cpp]`, `[source/transport/srt_transport.cpp]`, `[source/transport/rtmp_transport.cpp]`, `[source/transport/buffer_distributor.cpp]`, `[source/transport/transport_manager.cpp]`
- Risk: Protocol implementations may have race conditions, memory leaks, or incorrect state transitions that go undetected
- Priority: High

Untested area: Encoder pipeline building
- What's not tested: Pipeline string construction for each encoder/codec combination (12 combinations across AMD/NVENC/QSV/software x H264/H265/AV1), GStreamer element retrieval, buffer extraction
- Files: `[source/encode/encode.cpp:351-524]`
- Risk: Pipeline build errors produce silent failures; incorrect caps or element names cause runtime crashes
- Priority: High

Untested area: Statistics aggregation
- What's not tested: `statistics_aggregator.cpp` consensus bitrate calculation, stale stream pruning, multi-stream aggregation
- Files: `[source/stats/statistics_aggregator.cpp]`, `[source/stats/stats.cpp]`
- Risk: Incorrect bitrate adjustment could cause buffer overflows or quality degradation in production
- Priority: Medium

Untested area: UI threading
- What's not tested: FLTK UI updates from background threads, log append race conditions, callback coordination
- Files: `[source/ui/ui.cpp:466-823]`
- Risk: Intermittent crashes under load, data corruption in log displays
- Priority: High

Current test file: `test/source/open-broadcast-encoder_test.cpp` contains only 1 test that checks `library.name == "open-broadcast-encoder"`. The Catch2 dependency is in `vcpkg.json:16-19` but not wired into `CMakeLists.txt` for a test executable target (no `add_test()` or `enable_testing()` found).

## Downstream Impact Ranking

| Rank | Concern | Blocks | Severity | Fix effort |
|------|---------|--------|----------|------------|
| 1 | UI log appends not thread-safe (FLTK lock commented out) | Blocks: transport stats display, encode error logging, debug/troubleshooting workflow, any multi-threaded output. All change-routing rows involving output stats or logging depend on this. | critical | small |
| 2 | SRT/RTMP transports are stubs | Blocks: SRT output change-routing row, RTMP output change-routing row, streaming to YouTube/Facebook/Twitch workflows, multi-protocol streaming feature. These are independent change-routing targets that cannot proceed until stubs are implemented. | critical | large |
| 3 | Buffer distributor `distribute_legacy()` uses no-op deleter on raw pointer | Blocks: any change-routing row involving multi-output distribution, protocol additions that use legacy buffer path, performance optimization work. This is a dependency for every new transport type that uses the distributor. | critical | medium |
| 4 | No CMake test target / only 1 test | Blocks: all regression testing, CI/CD pipeline setup, safe refactoring of any module. Every change-routing row requires regression verification but no framework exists. | moderate | small |
| 5 | `std::stoi` on bitrate without validation | Blocks: UI input changes, bitrate adjustment work, any feature that reads encode_config.bitrate. This is a dependency for safe user input handling across the app. | moderate | small |
