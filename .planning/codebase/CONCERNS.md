# Codebase Concerns

**Analysis Date:** 2026-05-02

## Tech Debt

Area/component: GStreamer buffer lifetime management
- Issue: `pull_video_buffer()` and `pull_audio_buffer()` map a GStreamer buffer, unreference the sample, then return a raw pointer into the buffer's memory. The mapped memory becomes invalid once the caller returns, and `transport::send_buffer()` dereferences this dangling pointer. This is a use-after-free bug in the hot path.
- Files: `source/encode/encode.cpp:464-478`, `source/encode/encode.cpp:480-494`, `source/transport/transport.cpp:72-75`
- Impact: Data corruption or crashes during video transport; undefined behavior on every frame.
- Fix approach: Copy buffer data into `buffer_data` before unreferencing the sample, or use `gst_buffer_ref()` to extend lifetime until `send_buffer()` completes.

Area/component: Pipeline string formatting with wrong parser elements
- Issue: H265 encoder pipelines (AMD, QSV) and NVENC AV1 pipeline use `h264parse` instead of the correct parser (`h265parse` or `av1parse`). The QSV H265 pipeline at line 252 and the NVENC AV1 pipeline at line 284 both use `h264parse config-interval=1` which will fail at pipeline parse time for non-H264 streams.
- Files: `source/encode/encode.cpp:225` (AMF H265), `source/encode/encode.cpp:252` (QSV H265), `source/encode/encode.cpp:284` (NVENC AV1)
- Impact: Pipeline parse failure when H265 or AV1 codec is selected with AMD or QSV encoder; NVENC AV1 silently falls back to x264 (software encoder) instead of using NVENC hardware.
- Fix approach: Use `h265parse` for H265 pipelines and `av1parse` for AV1 pipelines consistently. The NVENC AV1 function should use `nvav1enc` or fall back to `rav1enc` with a clear comment.

Area/component: SDP input is hardcoded, not user-configurable
- Issue: `pipeline_build_source()` at line 38-49 uses a hardcoded SDP string for the SDP input mode. The `btn_open_sdp` button exists in the UI but its callback is never wired up. The `preview_input()` function at `source/main.cpp:96-98` has an empty SDP case.
- Files: `source/encode/encode.cpp:38-64`, `source/main.cpp:96-98`
- Impact: SDP mode is unusable -- cannot receive actual SDP/RTP streams.
- Fix approach: Add file dialog for SDP file loading, store the SDP string in `input_config`, and pass it to `pipeline_build_source()` via `std::format`.

Area/component: Global mutable state in main.cpp
- Issue: `main.cpp` uses 5 global mutable variables (`library app`, `transport* transporter`, `user_interface ui`, `encode* ptr_encoder`, `ndi_input ndi`). `ptr_encoder` holds a pointer to a stack-allocated `encode` object created inside `run_loop()`, creating a dangling pointer risk if the callback fires after the function returns.
- Files: `source/main.cpp:18-33`
- Impact: Race conditions on global state; dangling pointer dereference when RIST stats callback fires after encoder scope exits.
- Fix approach: Move encoder to heap with `std::shared_ptr`, store state in a proper context object, or use RAII wrappers.

## Known Bugs

Bug: NVENC AV1 encoder uses x264enc instead of NVENC AV1 encoder
- Symptoms: Selecting NVENC + AV1 produces H264 output instead of AV1; pipeline may succeed but encode the wrong codec.
- Files: `source/encode/encode.cpp:284`
- Trigger: Select NVENC encoder, AV1 codec, start encoding.
- Workaround: Use QSV + AV1 or Software + AV1 instead.

Bug: QSV H265 encoder pipeline uses h264parse
- Symptoms: Pipeline parse fails with "no element h264parse" when QSV + H265 is selected.
- Files: `source/encode/encode.cpp:252`
- Trigger: Select QSV encoder, H265 codec, start encoding.
- Workaround: Use AMD + H265 or Software + H265.

Bug: AMF H265 encoder pipeline uses h264parse
- Symptoms: Pipeline parse fails with "no element h264parse" when AMD + H265 is selected.
- Files: `source/encode/encode.cpp:225`
- Trigger: Select AMD encoder, H265 codec, start encoding.
- Workaround: Use QSV + H265 or Software + H265.

Bug: audio_sink is commented out in pipeline
- Symptoms: `audio_sink` member is always nullptr; `pull_audio_buffer()` dereferences nullptr; audio path is completely non-functional.
- Files: `source/encode/encode.cpp:79`, `source/encode/encode.cpp:372`, `source/encode/encode.cpp:480-494`
- Trigger: Any encoding session; accessing audio buffer.
- Workaround: None -- audio is entirely broken. Uncomment line 79.

Bug: SDP preview is a no-op
- Symptoms: Clicking "Preview Input" with SDP mode selected does nothing.
- Files: `source/main.cpp:96-98`
- Trigger: Select SDP input, click Preview Input.
- Workaround: Not available -- SDP preview must be implemented.

## Security Considerations

Area: No input validation on network addresses and ports
- Risk: Malformed or excessively large port values in RIST address, input port, or SDP strings could cause buffer overflows or unexpected behavior in URL parsing and GStreamer pipeline construction.
- Files: `source/transport/transport.cpp:45-60`, `source/encode/encode.cpp:53-56`, `source/ui/ui.cpp:564-567`
- Current mitigation: None -- no validation on user input before pipeline construction.
- Recommendations: Validate port numbers are in range (1-65535), validate IP address format, sanitize strings before passing to `std::format` for pipeline construction.

Area: GStreamer debug output in logs
- Risk: Pipeline error debug info may contain sensitive network topology information (ports, IPs) in production logs.
- Files: `source/encode/encode.cpp:434-438`, `source/ndi_input/ndi_input.cpp:88-92`
- Current mitigation: None.
- Recommendations: Sanitize log output to remove IP addresses and port numbers in non-debug builds.

Area: NDI device names from GStreamer are not freed
- Risk: Memory leak of GStrdup-allocated strings returned by `gst_device_get_display_name()`. Each call allocates memory that is never freed.
- Files: `source/ndi_input/ndi_input.cpp:47-48`
- Current mitigation: None.
- Recommendations: Call `g_free()` on each device name after use, or store in a container that manages lifetime.

## Performance Bottlenecks

Operation: Cumulative stats average computation
- Problem: `std::accumulate` walks the entire vector every time statistics are processed (every ~1 second during streaming). Vectors grow unbounded.
- Files: `source/stats/stats.cpp:43-52`
- Measurement: O(n) per call where n = number of seconds of streaming. After 1 hour: 3600 elements per vector.
- Suspected cause: Online average algorithm with incremental update is simple but the current code re-does full accumulation instead of maintaining running totals.
- Improvement path: Replace vectors with circular buffers of fixed size (e.g., 60 entries) and use incremental average: `avg = avg + (new_value - avg) / count`.

Operation: Unbounded vector growth in cumulative_stats
- Problem: Four `std::vector<int>` members in `cumulative_stats` grow without limit for the entire lifetime of the application.
- Files: `source/lib/lib.h:42-45`, `source/stats/stats.cpp:38-41`
- Measurement: Each vector grows by ~1 entry per second. After 24 hours: ~86,400 ints per vector = ~1.4 MB total.
- Suspected cause: Design choice to track all history, but no cleanup mechanism.
- Improvement path: Use `std::deque` with a max size, or circular buffer, or simply drop old entries.

Operation: GStreamer bus polling with 1ms sleep
- Problem: `play_pipeline()` polls the GStreamer bus in a tight loop with 1ms sleep, consuming CPU even when idle.
- Files: `source/encode/encode.cpp:377-391`
- Measurement: ~1000 iterations/second when idle, each doing a pthread mutex acquire/release.
- Suspected cause: GStreamer bus has no non-blocking API; sleep is a workaround.
- Improvement path: Use `gst_bus_set_sync_handler()` for synchronous message handling instead of polling.

## Fragile Areas

Component: Encoder pipeline builder cascade
- Files: `source/encode/encode.cpp:122-337`
- Why fragile: 20+ pipeline builder methods form a deep call cascade (encoder -> vendor -> codec). A single wrong element name in any pipeline string causes silent parse failure. Three bugs already exist (h264parse in H265/AV1 pipelines). No validation exists before `gst_parse_launch()`.
- Safe modification: Add a test harness that constructs each pipeline string and validates element existence before running. Use a mapping table instead of cascade.
- Test coverage: Zero. No pipeline string is ever validated outside of runtime.

Component: UI log functions without thread safety
- Files: `source/ui/ui.cpp:443-458`
- Why fragile: `transport_log_append()` and `encode_log_append()` call `Fl_Text_Display::insert()` without acquiring `Fl::lock()`, while `stats::got_rist_statistics()` at `source/stats/stats.cpp:56-75` does use lock/unlock. The RIST stats callback updates UI elements under lock, but log appends from other threads (transport log, NDI preview) do not.
- Safe modification: Add `Fl::lock()`/`Fl::unlock()` around all `Fl_Text_Display::insert()` calls, or use `Fl::awake_idle()` for thread-safe UI updates.
- Test coverage: None.

Component: NDI device monitor thread lifecycle
- Files: `source/ndi_input/ndi_input.cpp:16-37`, `source/ndi_input/ndi_input.h:21-24`
- Why fragile: `device_monitor_thread` runs a `while(run_monitor)` loop that is never joined in the destructor. If `ndi_input` is destroyed while the thread is running, `device_monitor` and `run_monitor` become dangling references. The `preview()` method has the same issue with `preview_thread`.
- Safe modification: Call `gst_device_monitor_stop()` and `device_monitor_thread.join()` in destructor. Set `run_monitor = false` before joining.
- Test coverage: None.

Component: RIST URL parsing with homer::url
- Files: `source/url/url.cc:1-512`, `source/transport/transport.cpp:45`
- Why fragile: The URL parser is a vendored third-party library (homer::url v0.3.0) with no tests. It uses `std::string_view` (`parse_target`) that references `whole_url_storage` which is copied at construction. If the source string is modified or destroyed, the view dangles. The parser also does not handle the `rist://` scheme's custom query parameter format robustly.
- Safe modification: Ensure `whole_url_storage` outlives `parse_target` usage. Add URL validation before passing to RIST.
- Test coverage: None.

Component: GStreamer pipeline error recovery
- Files: `source/encode/encode.cpp:351-375`, `source/encode/encode.cpp:398`
- Why fragile: `parse_pipeline()` logs errors but does not set any error state. `run_encode_thread()` calls `gst_element_set_state()` on a potentially null `datasrc_pipeline` at line 398. No null checks before any GStreamer element operations.
- Safe modification: Add null checks before every GStreamer API call. Set a failure state if pipeline parse fails. Return early from `run_encode_thread()` on failure.
- Test coverage: None.

## Dependency Risks

Dependency: GStreamer 1.28+
- Risk: Requires GStreamer 1.28+, which may not be available on older Linux distributions (Ubuntu 22.04 ships 1.20, Ubuntu 24.04 ships 1.24). The project needs 1.28+ for newer features.
- Impact: Build failure on systems with older GStreamer.
- Mitigation: Add GStreamer PPA or build from source. Document minimum OS requirements.

Dependency: NDI SDK (NewTek)
- Risk: Proprietary SDK with licensing restrictions. Free version has limitations; production use may require paid license.
- Impact: Cannot distribute software without NDI license compliance.
- Mitigation: Verify licensing terms. Consider WebRTC or SRT as open alternatives for NDI mode.

Dependency: FLTK (git submodule)
- Risk: Flaky updates via git submodule; version drift between local copy and upstream.
- Impact: Build breaks if submodule is not initialized or is at incompatible commit.
- Mitigation: Document submodule init in README. Pin to specific commit hash.

Dependency: rist-cpp (git submodule)
- Risk: Same as FLTK. Additionally, rist-cpp wraps the RIST protocol which has its own dependency on librist.
- Impact: Build complexity with nested C dependencies.
- Mitigation: Pin to specific commit. Test CI with submodule init.

Dependency: homer::url (vendored)
- Risk: Third-party vendored code with no maintenance guarantee. Uses `std::string_view` which has lifetime issues.
- Impact: URL parsing bugs may go undetected; hard to update.
- Mitigation: Replace with a well-maintained URL parser or inline a minimal custom parser for rist:// URLs only.

Dependency: Catch2 (test framework)
- Risk: Required for tests but only 1 test exists. CMakeLists.txt at `test/CMakeLists.txt:1-3` notes that testing depends on parent project including the test subdirectory.
- Impact: Tests are opt-in and not built by default.
- Mitigation: Add `add_subdirectory(test)` to main CMakeLists.txt when testing is enabled.

## Missing Critical Features

Feature gap: SDP file loading
- Problem: SDP mode uses a hardcoded SDP string. Users cannot load actual SDP files to receive RTP streams.
- Blocks: All SDP/RTP input workflows.

Feature gap: MPEG-TS preview
- Problem: `preview_input()` has an empty case for `input_mode::mpegts`.
- Blocks: Previewing MPEG-TS input before encoding.

Feature gap: Audio pipeline
- Problem: Audio sink is commented out in the pipeline (`source/encode/encode.cpp:79`). Audio encoding path is non-functional.
- Blocks: Any use case requiring audio output.

Feature gap: Save/load configuration
- Problem: No mechanism to persist encoder settings between sessions. All settings reset to defaults on restart.
- Blocks: Production deployment where settings need to be consistent.

## Test Coverage Gaps

Untested area: Encoder pipeline construction
- What's not tested: All 20+ pipeline builder methods, codec/encoder combinations, pipeline parse success/failure.
- Files: `source/encode/encode.cpp` (all `pipeline_build_*` methods)
- Risk: Pipeline strings with wrong elements (as seen with h264parse in H265 pipelines) go undetected until runtime.
- Priority: High

Untested area: Transport layer
- What's not tested: RIST URL construction, multi-stream port offset logic, buffer sending, statistics callback chain.
- Files: `source/transport/transport.cpp`, `source/transport/transport.h`
- Risk: Port conflicts with multiple streams, incorrect RIST URL construction.
- Priority: High

Untested area: Stats and bitrate adaptation
- What's not tested: Bitrate adjustment algorithm, cumulative stats computation, edge cases (zero quality, zero bitrate).
- Files: `source/stats/stats.cpp`, `source/stats/stats.h`
- Risk: Incorrect bitrate values corrupt encoder behavior; memory leaks from unbounded vectors.
- Priority: High

Untested area: NDI device discovery
- What's not tested: Device monitor thread lifecycle, device name memory management, preview pipeline cleanup.
- Files: `source/ndi_input/ndi_input.cpp`
- Risk: Memory leaks, crashes on device disconnect.
- Priority: Medium

Untested area: URL parsing
- What's not tested: rist:// URL parsing, malformed URLs, edge cases (IPv6, ports, query parameters).
- Files: `source/url/url.cc`
- Risk: Incorrect host/port extraction breaks RIST transport.
- Priority: Medium

Untested area: UI callbacks and threading
- What's not tested: Callback chains, thread-safe UI updates, menu selection handling.
- Files: `source/ui/ui.cpp`
- Risk: UI crashes from concurrent access, incorrect config reads.
- Priority: Medium

Untested area: Main application lifecycle
- What's not tested: Start/stop sequence, encoder lifecycle, global state management.
- Files: `source/main.cpp`
- Risk: Dangling pointers, resource leaks on shutdown.
- Priority: High

## Downstream Impact Ranking

| Rank | Concern | Blocks | Severity | Fix effort |
|------|---------|--------|----------|------------|
| 1 | GStreamer buffer use-after-free (dangling pointer from pull_video_buffer) | All transport changes, new output formats, performance optimization | critical | medium |
| 2 | Encoder pipeline builder wrong parser elements (h264parse in H265/AV1) | Any codec/encoder combination work, new encoder additions, H265/AV1 support | critical | small |
| 3 | Unbounded stats vector growth + O(n) average recomputation | Any stats-related work, long-running streaming sessions, new telemetry features | moderate | small |

Ranking rationale:
1. The buffer use-after-free is the highest priority because it affects every frame sent over the network and can cause data corruption or crashes at any time. It blocks all transport-related improvements.
2. The wrong parser elements block H265 and AV1 encoding entirely for AMD and QSV hardware. This is a small fix (change parser element name) but blocks a major feature set.
3. The unbounded vector growth is a slow memory leak that degrades over time. It blocks long-running streaming reliability and any stats-related enhancements.
