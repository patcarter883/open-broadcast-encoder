# Codebase Concerns

**Analysis Date:** 2026-05-02

## Security Considerations

Area: URL credentials exposure
- Risk: `source/url/url.h:73` stores parsed URL password in plaintext as `std::string password`. This field is returned by `getPassword()` and could be logged or displayed. RIST URLs with credentials would pass plaintext passwords through the parser.
- Files: `[source/url/url.h:73]`, `[source/url/url.cc:338]`
- Current mitigation: None. No sanitization of URL components.
- Recommendations: Add a `stripCredentials()` method or use tokenized references instead of storing raw passwords.

Area: NDI device names
- Risk: `source/ndi_input/ndi_input.cpp:47` uses `gst_device_get_display_name()` which returns malloc'd strings stored as `char*` in `std::vector<char*>`. These strings are never freed, creating a memory leak that grows with each device refresh.
- Files: `[source/ndi_input/ndi_input.cpp:39-52]`, `[source/ui/ui.cpp:537]`
- Current mitigation: None.
- Recommendations: Return `std::vector<std::string>` instead of `std::vector<char*>` for proper ownership semantics. Call `g_free()` on each device name after use.

Area: SDP string injection
- Risk: The hardcoded SDP string in `source/encode/encode.cpp:38-49` is used directly in `std::format` for the pipeline string. If this were ever made user-configurable, special characters in the SDP could break the GStreamer pipeline string syntax.
- Files: `[source/encode/encode.cpp:58-64]`
- Current mitigation: SDP is hardcoded, not user-configurable.
- Recommendations: When SDP becomes user-configurable, escape or validate the SDP content before inserting into the pipeline string.

## Tech Debt

Area: Wrong GStreamer parser elements for H.265
- Issue: H.265 encoder pipelines use `h264parse` instead of `h265parse`, which will fail at pipeline parse time or produce corrupted output. The same element name is used in both QSV and software H.265 encoder builders.
- Files: `[source/encode/encode.cpp:252]`, `[source/encode/encode.cpp:301]`
- Impact: H.265 encoding silently fails or produces garbage. Users selecting H.265 codec get non-functional pipelines with no error indication beyond GStreamer bus error messages.
- Fix approach: Replace `h264parse config-interval=1` with `h265parse config-interval=1` in both `pipeline_build_qsv_h265_encoder()` and `pipeline_build_software_h265_encoder()`.

Area: NVENC AV1 encoder uses wrong element
- Issue: `pipeline_build_nvenc_av1_encoder()` at line 284 uses `x264enc` (software H.264 encoder) instead of a proper NVENC AV1 element or even an NVENC element at all. This is clearly a copy-paste bug.
- Files: `[source/encode/encode.cpp:284-287]`
- Impact: Selecting NVENC + AV1 silently falls back to software H.264 encoding, wasting the GPU encoder and producing wrong codec output.
- Fix approach: Replace with proper NVENC AV1 element (e.g., `nvv1dec` -> `nvv4l2h265enc` pipeline or use `nvv4l2av1enc` if available in the GStreamer version). Validate the element exists at runtime.

Area: Uninitialized `cumulative_stats` fields
- Issue: `source/lib/lib.h:50-51` defines `current_bitrate` and `previous_quality` without default values in an aggregate struct. The `library` constructor at `source/lib/lib.cpp:3-5` does not initialize them.
- Files: `[source/lib/lib.h:40-52]`, `[source/lib/lib.cpp:3-5]`, `[source/stats/stats.cpp:14]`
- Impact: `stats->previous_quality` starts as garbage. The comparison at `stats.cpp:14` (`if (stats->previous_quality > 0 && ...)`) will produce unpredictable behavior on first stats callback. Bitrate adaptation is non-deterministic.
- Fix approach: Add default initializers: `int current_bitrate = 4300;` and `double previous_quality = 0;` in the struct definition.

Area: Double pipeline/bus cleanup
- Issue: `stop_encode_thread()` at `source/encode/encode.cpp:402-426` calls `gst_element_set_state()`, `gst_object_unref()` for both pipeline and bus. The destructor at `source/encode/encode.cpp:21-33` does the same cleanup. If `stop_encode_thread()` is called before destruction, the destructor will double-unref already-freed GStreamer objects.
- Files: `[source/encode/encode.cpp:22-33]`, `[source/encode/encode.cpp:402-426]`
- Impact: Double-free / use-after-free of GStreamer objects, leading to crashes or heap corruption.
- Fix approach: Add a `pipeline_cleaned_up` boolean flag. `stop_encode_thread()` checks and sets it. Destructor only cleans up if not already cleaned. Or set `datasrc_pipeline = nullptr` and `bus = nullptr` after unref in `stop_encode_thread()`.

Area: Commented-out lock calls in UI log append
- Issue: `source/ui/ui.cpp:443-448` and `source/ui/ui.cpp:556-558` have `Fl::lock()` / `Fl::unlock()` / `Fl::awake()` calls commented out, with the active code being just `insert()` on `Fl_Text_Display`. These methods are called from background threads (RIST callback, encode callback).
- Files: `[source/ui/ui.cpp:443-448]`, `[source/ui/ui.cpp:556-558]`
- Impact: Undefined behavior when background threads modify FLTK widget state without locking. Can cause crashes, corrupted text display, or frozen UI. FLTK is not thread-safe.
- Fix approach: Uncomment the `Fl::lock()` / `Fl::unlock()` / `Fl::awake()` calls. Ensure `encode_log_append` and `transport_log_append` are the only paths used for cross-thread UI updates.

Area: `ptr_encoder` dangling pointer
- Issue: `source/main.cpp:21` declares `encode* ptr_encoder` as a global raw pointer. `source/main.cpp:63` sets it to `&encoder` where `encoder` is a stack-local variable in `run_loop()`. The `rist_stats_cb` callback at `source/main.cpp:53` dereferences `ptr_encoder` on every RIST statistics update.
- Files: `[source/main.cpp:21]`, `[source/main.cpp:53]`, `[source/main.cpp:60-63]`
- Impact: If `run_loop()` returns (encoder thread finishes) and RIST statistics arrive afterward, `ptr_encoder` is a dangling pointer. The callback dereferences freed stack memory, causing undefined behavior or crash.
- Fix approach: Use `std::shared_ptr<encode>` for `ptr_encoder`, or store the encoder in the global `library` struct instead of on the stack. Add null-check in `rist_stats_cb`.

Area: NDI preview never stops
- Issue: `source/ndi_input/ndi_input.cpp:107-108` creates a `g_main_loop` and calls `g_main_loop_run()` which blocks indefinitely after the preview pipeline reaches ERROR or EOS. The pipeline cleanup at lines 111-113 is unreachable.
- Files: `[source/ndi_input/ndi_input.cpp:107-113]`
- Impact: NDI preview window freezes after the first preview ends. The preview thread hangs forever and cannot be terminated. No way to start a new preview.
- Fix approach: Remove the `g_main_loop` block. Use the existing bus message handling (lines 75-104) as the control flow terminator. Set pipeline to NULL state and clean up within the error/EOS handling branch.

Area: Stale SDP constant string
- Issue: `source/encode/encode.cpp:38-49` hardcodes a fixed SDP string with hardcoded IP `127.0.0.1`, timestamp `1443716955`, and MAC address. This is never used in the actual SDP code path because the `sdpsrc` at line 60-63 wraps it in a pipeline string but the SDP content is static.
- Files: `[source/encode/encode.cpp:38-49]`
- Impact: If user provides a real SDP (not the hardcoded stub), this constant is irrelevant. The hardcoded SDP may cause confusion during debugging. The SDP source selection at line 58-64 is unreachable for real SDP input since user input is not captured anywhere.
- Fix approach: Either wire up SDP file/URL input in the UI (btn_open_sdp at `source/ui/ui.cpp:182-184` has no callback wired), or remove the hardcoded constant and log a warning when SDP mode is selected without user input.

## Known Bugs

Bug: `pull_video_buffer()` returns dangling pointer to GStreamer buffer
- Symptoms: Encoded video data sent over RIST contains garbage, corrupted frames, or program crashes.
- Files: `[source/encode/encode.cpp:464-478]`
- Trigger: Every time video is encoded and the main loop calls `pull_video_buffer()` at `source/main.cpp:68-71`.
- Details: Line 471 calls `gst_buffer_map()` which returns a pointer `info.data` into the GStreamer buffer. Line 472 calls `gst_sample_unref(sample)` which releases the sample's reference to the buffer. The returned `buffer_data` contains this stale pointer. Line 473 returns the struct, and the caller at `source/main.cpp:70` calls `transporter->send_buffer(vidbuf, 0)` which reads from the now-unmapped buffer.
- Repro: Start encoding any video source. Observe corrupted output on the RIST receiver, or crashes.
- Workaround: None. This is a fundamental bug that makes video streaming non-functional.
- Fix approach: Copy the buffer data before unreferencing the sample. Change `buffer_data` to use `std::vector<uint8_t>` or `g_memdup()` the data. Or hold a reference to the sample and only unreference after `send_buffer()` completes (requires async send or callback).

Bug: Stats vectors grow unbounded, O(n) accumulate on every call
- Symptoms: UI becomes progressively slower as the program runs. Memory usage grows without bound.
- Files: `[source/lib/lib.h:42-45]`, `[source/stats/stats.cpp:38-52]`
- Trigger: Every RIST statistics callback (happens periodically, e.g., every 1-2 seconds).
- Details: Lines 38-41 push new values into `bandwidth`, `encode_bitrate`, `retransmitted_packets`, and `total_packets` vectors that are never cleared. Lines 43-52 call `std::accumulate` over the entire vector each time, which is O(n) where n grows over time. After 1 hour of operation (~3000 samples), each stats call processes 3000 elements.
- Repro: Run the application for several hours. Observe increasing UI latency and memory usage.
- Workaround: Restart the application periodically.
- Fix approach: Cap vector size to a sliding window (e.g., last 100 samples). Use incremental online algorithm for running averages instead of full `std::accumulate` on every call.

Bug: Test file references non-existent member
- Symptoms: Build fails if test infrastructure is enabled. Test cannot compile.
- Files: `[test/source/open-broadcast-encoder_test.cpp:8]`
- Trigger: Any build with `BUILD_TESTING=ON`.
- Details: Test checks `lib.name == "open-broadcast-encoder"` but `library` struct in `source/lib/lib.h:76-99` has no `name` member.
- Workaround: Remove or fix the test.
- Fix approach: Add `std::string name` member to `library` struct, or remove the broken test.

## Performance Bottlenecks

Operation: Cumulative stats averaging
- Problem: `std::accumulate` over entire history vectors on every stats callback
- Files: `[source/stats/stats.cpp:43-52]`
- Measurement: O(n) where n is total elapsed stats calls. After 10 minutes (~600 calls), 2400 integers processed per call. After 1 hour (~3600 calls), 14400 integers per call.
- Suspected cause: Design choice to maintain "cumulative" averages requires full vector scan. Vectors never bounded or pruned.
- Improvement path: Replace with incremental running average (online algorithm with single accumulator and counter). Cap at sliding window of last N samples.

Operation: NDI device refresh
- Problem: `source/ndi_input/ndi_input.cpp:47` allocates memory with `gst_device_get_display_name()` on every refresh, storing raw pointers in `vector<char*>` with no deallocation.
- Files: `[source/ndi_input/ndi_input.cpp:39-52]`
- Measurement: Memory leak proportional to number of NDI devices times number of refreshes.
- Suspected cause: GStreamer API returns malloc'd strings; caller expected to free them but ownership is not transferred.
- Improvement path: Use `std::vector<std::string>` with `g_strndup()` + proper free, or use `Glib::ustring` which handles GStreamer string ownership correctly.

## Fragile Areas

Component: Encoder dispatch tree
- Files: `[source/encode/encode.cpp:122-311]`
- Why fragile: 12 encoder implementations (4 vendors x 3 codecs) built via nested switch-case dispatch. Copy-paste bugs are common (see H.265 using `h264parse` at lines 252, 301; NVENC AV1 using `x264enc` at line 284). No compile-time validation that the GStreamer element string is correct.
- Safe modification: Add a `validate_element()` function that checks the element string against `gst_element_factory_find()`. Use constexpr string arrays instead of inline format strings. Add unit tests for each encoder combination.
- Test coverage: None. No tests exercise any encoder pipeline path.

Component: FLTK callback macros
- Files: `[source/ui/ui.cpp:618-695]`, `[source/ui/ui.cpp:19-139]`
- Why fragile: `FL_METHOD_CALLBACK_*` macros from FLTK use variadic macros that are fragile with type mismatches. Menu arrays use raw integer casts (`(void*)(static_cast<long>(encoder::amd))`) for user data. Changes to enum values or types silently break the cast. The `choose_input_protocol` callback at `source/ui/ui.cpp:485` switches on hardcoded integers (1, 2, 3) rather than enum values.
- Safe modification: Replace magic integer casts with `reinterpret_cast` wrappers in dedicated helper functions. Replace switch on integers with switch on enum values retrieved via helper.
- Test coverage: None. UI callbacks are not unit testable without a display server.

Component: Global state in main.cpp
- Files: `[source/main.cpp:18-33]`
- Why fragile: 5 global mutable/static objects (`library app`, `transporter`, `ui`, `ptr_encoder`, `ndi`) with complex interdependencies. `ndi` at line 33 is constructed with `&encode_log` which references `ui` before `ui.init_ui()` is called at line 118. The `library` struct holds all shared state, creating tight coupling between all layers.
- Safe modification: Pass dependencies through a context object instead of globals. Use dependency injection for callbacks. Remove global `ptr_encoder` in favor of `shared_ptr<encode>` or callback registration.
- Test coverage: None. Global state makes unit testing impossible without mocking.

Component: RIST statistics callback thread safety
- Files: `[source/main.cpp:50-55]`, `[source/stats/stats.cpp:6-77]`, `[source/transport/transport.cpp:32-37]`
- Why fragile: RIST library calls `statistics_callback` from an internal background thread. The callback at `main.cpp:50` calls `stats::got_rist_statistics()` which mutates `stats->current_bitrate` (line 33-34), pushes to vectors (lines 38-41), and updates UI elements (lines 56-75). All of this happens from the RIST thread with no mutex protection on the shared `cumulative_stats` struct. The `library::is_running` atomic at `source/lib/lib.h:87` is the only thread-safe field.
- Safe modification: Protect `cumulative_stats` mutations with a mutex. Queue stats updates to the main thread via a lock-free queue or `Fl::awake()` callback. Use `std::atomic` for `current_bitrate`.
- Test coverage: None. Thread safety issues are nearly impossible to test deterministically.

Component: GStreamer pipeline error recovery
- Files: `[source/encode/encode.cpp:351-375]`, `[source/encode/encode.cpp:428-462]`
- Why fragile: Parse errors at line 360-362 are logged but `datasrc_pipeline` remains nullptr, and the code continues. `parse_pipeline()` at line 367-372 calls `gst_bin_get_by_name()` on a nullptr pipeline. `play_pipeline()` at line 398 calls `gst_element_set_state()` on nullptr. No null-checks protect against null pipeline usage.
- Safe modification: Return early from `parse_pipeline()` if `datasrc_pipeline` is nullptr. Add null checks before all GStreamer element operations. Set `encoder_running = false` on parse failure.
- Test coverage: None.

## Dependency Risks

Dependency: GStreamer 1.28 requirement
- Risk: `CMakeLists.txt:32` requires `gstreamer-1.0>=1.28`. GStreamer 1.28 may not be available on many Linux distributions (Ubuntu 24.04 ships 1.24, Debian 12 ships 1.22). This blocks deployment on stable OS images.
- Impact: All GStreamer pipelines (encode, NDI input, SDP input) become non-functional. Application cannot be built on older systems.
- Mitigation: Lower version requirement to 1.24 (widely available) and add runtime checks for newer features. Use `pkg_check_modules` with version ranges and provide fallbacks.

Dependency: FLTK 1.4.4 (git submodule)
- Risk: `external/fltk` is at `release-1.4.4`. FLTK 1.4.x has breaking API changes from 1.3.x. The code uses FLTK 1.4 APIs (`Fl_Flex`, `Fl_Grid`, `FL_METHOD_CALLBACK_*` macros from `fl_callback_macros.H`).
- Impact: Any FLTK upgrade or downgrade requires code changes. The submodule is a full vendored copy (~10MB) which increases build time.
- Mitigation: Pin the submodule to a specific commit. Consider using system FLTK via pkg-config instead of vendoring, to reduce build complexity.

Dependency: rist-cpp (git submodule, master branch)
- Risk: `external/rist-cpp` is at `master-28-g59bb3a3` (rolling HEAD, not a tagged release). No version pin.
- Impact: Builds may break on any upstream change to rist-cpp. The TODO comments in rist-cpp (`RISTNet.cpp:265,611`) indicate known issues that could affect stability.
- Mitigation: Pin to a tagged release or specific commit hash. Monitor rist-cpp upstream for breaking changes.

Dependency: NDI SDK (system package)
- Risk: `CMakeLists.txt:29` uses `find_package(NDI REQUIRED)` requiring system-wide NDI SDK installation. NDI SDK requires a NewTek account and has licensing restrictions.
- Impact: Cannot build without NDI SDK installed and licensed. NDI device discovery (`source/ndi_input/ndi_input.cpp:16-37`) will fail silently if NDI is unavailable.
- Mitigation: Make NDI an optional dependency with `find_package(NDI QUIET)` and conditionally compile NDI code. Provide a stub/no-op NDI path for systems without the SDK.

## Missing Critical Features

Feature: SDP file/URL input
- Problem: `btn_open_sdp` at `source/ui/ui.cpp:182-184` exists but has no callback wired. The hardcoded SDP at `source/encode/encode.cpp:38-49` is the only SDP input mechanism. Users cannot load custom SDP files or SDP URLs.
- Blocks: Any use case requiring dynamic SDP configuration (live SDP from external source, SDP from file).
- Fix approach: Wire a file dialog callback to `btn_open_sdp`. Read the SDP file content and pass it to `pipeline_build_source()`.

Feature: Runtime pipeline restart
- Problem: Once encoding starts (`run_loop()` at `source/main.cpp:57`), there is no mechanism to change encoder settings mid-stream. The "Stop Encode" button sets `is_running = false` but does not properly clean up the encoder thread. Changing codec, encoder, or bitrate requires a full application restart.
- Blocks: Dynamic codec switching, bitrate adjustment mid-stream (adaptive bitrate only works within a session), pipeline reconfiguration.
- Fix approach: Implement a pipeline rebuild path in `stop_encode_thread()` that preserves the encode object but rebuilds the GStreamer pipeline with new config. Add a "Rebuild Pipeline" button to the UI.

## Test Coverage Gaps

Untested area: Encoding pipeline construction
- What's not tested: All `pipeline_build_*` methods in `source/encode/encode.cpp:35-349`. Pipeline string assembly for all 12 encoder combinations.
- Files: `[source/encode/encode.cpp]`
- Risk: Wrong GStreamer element strings (e.g., `h264parse` for H.265 at lines 252, 301) are not caught. Copy-paste bugs (NVENC AV1 using x264enc at line 284) go undetected.
- Priority: High

Untested area: Buffer data lifecycle
- What's not tested: `pull_video_buffer()` return value correctness, buffer memory validity after sample unref, `transporter->send_buffer()` data integrity.
- Files: `[source/encode/encode.cpp:464-494]`, `[source/main.cpp:68-71]`, `[source/transport/transport.cpp:72-75]`
- Risk: Use-after-free bug at `source/encode/encode.cpp:464-478` is invisible in unit tests without memory sanitizers. Only detectable through runtime crashes or corrupted output.
- Priority: Critical

Untested area: Statistics and bitrate adaptation
- What's not tested: `stats::got_rist_statistics()` algorithm at `source/stats/stats.cpp:6-77`. Bitrate delta calculation, vector accumulation, edge cases (first call with garbage `previous_quality`, quality=0, negative deltas).
- Files: `[source/stats/stats.cpp]`
- Risk: Uninitialized `previous_quality` in `cumulative_stats` causes undefined behavior on first call. Unbounded vector growth not caught by tests.
- Priority: High

Untested area: Threading and callback safety
- What's not tested: Cross-thread UI updates, `ptr_encoder` dangling pointer, RIST callback thread safety, encode thread lifecycle.
- Files: `[source/main.cpp:18-73]`, `[source/ui/ui.cpp:443-458]`, `[source/stats/stats.cpp:56-75]`
- Risk: Race conditions and use-after-free are non-deterministic. Only detectable with ThreadSanitizer or Valgrind over extended runtime.
- Priority: High

Untested area: URL parsing edge cases
- What's not tested: `homer6::url` parsing for malformed URLs, RIST-specific URLs with query parameters, IPv6 addresses, empty ports, unknown schemes.
- Files: `[source/url/url.cc]`
- Risk: `getPort()` at `source/url/url.cc:65` uses `std::atoi()` which silently returns 0 on parse failure. RIST URLs like `rist://127.0.0.1:5000` may parse incorrectly if the port segment includes extra characters.
- Priority: Medium

Untested area: NDI device management
- What's not tested: `refresh_devices()` memory leak, `preview()` hanging behavior, device monitor thread lifecycle.
- Files: `[source/ndi_input/ndi_input.cpp:39-115]`
- Risk: Memory leak on every device refresh. Preview hangs indefinitely.
- Priority: Medium

## Downstream Impact Ranking

Rank the top 3 concerns by how much future work they block. Uses the Change Routing table in ARCHITECTURE.md as reference.

| Rank | Concern | Blocks | Severity | Fix effort |
|------|---------|--------|----------|------------|
| 1 | Use-after-free in `pull_video_buffer()` (`source/encode/encode.cpp:464-478`) | All change-routing rows: New input source, New codec, New hardware encoder, New UI control. Every encoding pipeline path is broken at the buffer extraction layer. No encoder combination works. | critical | medium |
| 2 | Dangling `ptr_encoder` + unguarded callback (`source/main.cpp:21,53,63`) | New stats metric, New bitrate adaptation rule, New transport protocol. Any work on the statistics/adaptive bitrate feedback loop is unsafe because the callback dereferences a dangling pointer. Blocks all stats-related change-routing entries. | critical | small |
| 3 | Unbounded stats vectors + O(n) accumulate (`source/lib/lib.h:42-45`, `source/stats/stats.cpp:38-52`) | New stats metric, New bitrate adaptation rule. The cumulative_stats struct is the foundation for all stats-related work. Unbounded growth and O(n) recalculations make any stats enhancement impractical at runtime. Blocks progressive improvement to the stats layer. | moderate | small |

Ranking rationale:
- Rank 1 blocks every encoding-related change because the core data flow (pipeline -> buffer pull -> transport send) is fundamentally broken. No codec, encoder, or input source can work until this is fixed.
- Rank 2 blocks the entire stats/adaptive-bitrate subsystem referenced in 3 Change Routing rows (New stats metric, New bitrate adaptation rule, New transport protocol). The dangling pointer makes any stats work crash-prone.
- Rank 3 blocks future stats layer improvements. The cumulative_stats struct is referenced by "New stats metric" and "New bitrate adaptation rule" rows in Change Routing. Without bounded vectors and O(1) averaging, any new metrics or rules degrade over time.

---

*Concerns audit: 2026-05-02*
