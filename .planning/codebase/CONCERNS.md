# Codebase Concerns

**Analysis Date:** 2026-04-28

<guidelines>
- Every concern includes concrete file paths.
- Severity assigned per concern: critical / moderate / minor.
- This document covers risk and leverage: what breaks, where, and how to change safely.
</guidelines>

## Known Bugs

### Bug 1: NVENC AV1 Encoder Uses Wrong Element (x264 instead of NVENC AV1)
- **Symptoms:** Selecting NVENC encoder with AV1 codec produces H.264 output instead of AV1. The pipeline contains `x264enc` which is an H.264 software encoder, not an NVENC AV1 encoder.
- **Files:** `source/encode/encode.cppm` (line 356), `source/encode/encode.cpp` (line 284)
- **Trigger:** User selects NVENC encoder + AV1 codec, starts encoding. Pipeline parses with `x264enc name=videncoder` instead of a proper AV1 encoder.
- **Root cause:** Copy-paste error. The function `pipeline_build_nvenc_av1_encoder()` should use an NVENC AV1 element (e.g., `nvencav1enc` or `nvv4l2h265enc`-equivalent), but it uses `x264enc` which is also used by `pipeline_build_software_h264_encoder()`.
- **Safe fix:** Replace `x264enc` with `nvh265enc` or appropriate NVENC AV1 element. The nvenc_av1 GStreamer plugin (from `gstreamer1.0-plugins-bad`) should be `nvv4l2av1enc` or similar depending on GStreamer version.

### Bug 2: H.265 Encoders Use Wrong PARSER (h264parse instead of h265parse)
- **Symptoms:** H.265 encoded streams are passed through `h264parse`, which cannot parse H.265 elementary streams. Pipeline likely fails to parse or produces corrupt output.
- **Files:** `source/encode/encode.cppm` (lines 296-298, 372-374), `source/encode/encode.cpp` (lines 222-226, 299-303)
- **Trigger:** User selects H.265 codec with AMD, QSV, or software encoder. The pipeline string contains `! h264parse config-interval=1` after the H.265 encoder.
- **Root cause:** Copy-paste from H.264 encoder templates. The H.265 encoder builder functions all use `h264parse` instead of `h265parse`.
- **Safe fix:** Replace `h264parse` with `h265parse` in:
  - `pipeline_build_amd_h265_encoder()` (line 297)
  - `pipeline_build_qsv_h265_encoder()` (line 324)
  - `pipeline_build_software_h265_encoder()` (line 373)

### Bug 3: RIST URL Construction Missing `&` Separator for Bandwidth Parameter
- **Symptoms:** RIST URL is malformed: `rist://127.0.0.1:5000?bandwidth=6000buffer-min=245&...`. The `bandwidth` parameter value is concatenated directly with `buffer-min`, so `6000buffer-min` is parsed as the bandwidth value. RIST initialization likely fails or uses default bandwidth.
- **Files:** `source/transport/transport.cppm` (line 94), `source/transport/transport.cpp` (line 51)
- **Trigger:** Any encoding session with RIST transport. The URL construction formats `bandwidth=VALUEbuffer-min=VALUE` without the `&` separator.
- **Root cause:** Missing `&` in the format string on line 94.
- **Safe fix:** Change `"bandwidth={}buffer-min={}"` to `"bandwidth={}&buffer-min={}"`.

### Bug 4: `pull_video_buffer()` Returns Pointer to Mapped GStreamer Buffer Memory Without Unmapping
- **Symptoms:** Undefined behavior or data corruption. The `GstMapInfo` is obtained via `gst_buffer_map()` but never unmapped with `gst_buffer_unmap()`. The returned `buf_data` pointer points to mapped memory that may be invalid once the calling code proceeds. Additionally, `gst_sample_unref(sample)` is called before the caller has finished reading the buffer.
- **Files:** `source/encode/encode.cppm` (lines 538-550), `source/encode/encode.cpp` (lines 464-478)
- **Trigger:** Every call to `pull_video_buffer()` during encoding. The buffer data is then passed to `transporter->send_buffer()` in `source/main.cpp` (line 70).
- **Root cause:** Missing `gst_buffer_unmap()` call after `gst_buffer_map()`. The buffer is mapped but never unmapped, and the sample is unreferenced while the caller still needs the data.
- **Safe fix:** Either add `gst_buffer_unmap()` before returning, or allocate a copy of the buffer data and manage its lifetime properly.

### Bug 5: Audio Sink Element Never Exists in Pipeline
- **Symptoms:** `audio_sink` is always `nullptr`. The pipeline string in `pipeline_build_sink()` only creates `video_sink`. Any attempt to pull audio buffers via `pull_audio_buffer()` will dereference a null pointer.
- **Files:** `source/encode/encode.cppm` (lines 147-153, 443-444), `source/encode/encode.cpp` (lines 75-81, 371-372)
- **Trigger:** Any pipeline construction. The `audio_sink` field is set by `gst_bin_get_by_name(..., "audio_sink")` but no element named `audio_sink` exists in the pipeline string.
- **Root cause:** The audio path is commented out in `pipeline_build_sink()` (`// " appsink name=audio_sink  "`). The `audio_sink` member is queried from the pipeline but the element was never created.
- **Safe fix:** Either create the audio sink element in `pipeline_build_sink()` or remove the audio path entirely.

### Bug 6: NDI Preview Thread Leaks Main Loop
- **Symptoms:** Each NDI preview call leaks a `GMainLoop` allocation. The main loop is created but never destroyed, and the pipeline is cleaned up before the loop is stopped.
- **Files:** `source/ndi_input/ndi_input.cppm` (lines 134-141)
- **Trigger:** User clicks "Preview Input" with NDI source. The preview thread creates a `g_main_loop_new()` and runs it, but `g_main_loop_destroy()` is never called.
- **Root cause:** Missing `g_main_loop_destroy()` call after `g_main_loop_run()`. The pipeline state is set to NULL and unreferenced while the loop may still be running.

## Security Considerations

### Security 1: No Input Validation on Network Addresses and Ports
- **Risk:** User-provided address strings (RIST address, SDP port, listen port) are used directly in GStreamer pipeline strings and URL construction without validation. Malformed input could cause pipeline parse errors, crashes, or unexpected network behavior.
- **Files:** `source/ui/ui.cppm` (lines 651-659, 551-554), `source/encode/encode.cppm` (lines 125-128), `source/transport/transport.cppm` (lines 82-113)
- **Current mitigation:** FLTK Input widgets accept any string. No range checking on port numbers.
- **Recommendations:** Validate port numbers are in range 0-65535. Validate IP addresses using `std::istringstream` or similar. Sanitize input before embedding in GStreamer pipeline strings to prevent injection.

### Security 2: NDI Device Names Stored as Raw `char*` in UI Without Lifetime Management
- **Risk:** `gst_device_get_display_name()` returns allocated memory (caller must `g_free()`). These pointers are stored as `char*` in FLTK Choice widget user_data. If the device monitor updates and frees old devices, the UI holds dangling pointers. Memory is never freed.
- **Files:** `source/ndi_input/ndi_input.cppm` (lines 66-79), `source/ui/ui.cppm` (lines 624-635)
- **Current mitigation:** None. NDI device names are acquired via `gst_device_get_display_name()` and stored in `Fl_Choice` user_data without corresponding `g_free()`.
- **Recommendations:** Store device names as `std::string` instead of `char*`. Manage ownership explicitly.

### Security 3: Hardcoded SDP String in Source Code
- **Risk:** The default SDP string in `pipeline_build_source()` is hardcoded in source code. If an attacker can influence how this SDP is used, it could be exploited. Additionally, the hardcoded SDP references a specific MAC address (`00-02-c5-ff-fe-21-60-5c`).
- **Files:** `source/encode/encode.cppm` (lines 110-121), `source/encode/encode.cpp` (lines 38-49)
- **Current mitigation:** The SDP is only used as a default template when no custom SDP is loaded.
- **Recommendations:** Move the SDP template to a separate configuration file or resource. Do not embed network configuration in source.

### Security 4: Callback Function Pointer Invoked on Every Keystroke
- **Risk:** `input_rist_address_cb()` (line 656 of `ui.cppm`) calls the address callback function on every keystroke change, which triggers full RIST reconfiguration. This is not a security vulnerability per se, but it's a vector for resource exhaustion if combined with any other flaw.
- **Files:** `source/ui/ui.cppm` (lines 656-660)
- **Recommendations:** Debounce or require explicit "apply" action for address changes.

## Tech Debt

### Tech Debt 1: Dead Code — `.cppm` Module Files Not Compiled
- **Issue:** Every module has both `.cppm` (C++20 module interface) and `.cpp` (regular implementation) files. The `.cppm` files contain full implementations with `export module X;` declarations, but the CMakeLists.txt for each module only lists the `.cpp` file. The `.cppm` files are effectively dead code.
- **Files:** 
  - `source/encode/encode.cppm` (578 lines, NOT compiled) vs `source/encode/encode.cpp` (506 lines)
  - `source/transport/transport.cppm` (118 lines, NOT compiled) vs `source/transport/transport.cpp` (75 lines)
  - `source/ui/ui.cppm` (782 lines, NOT compiled) vs `source/ui/ui.cpp` (695 lines)
  - `source/ndi_input/ndi_input.cppm` (149 lines, NOT compiled) vs `source/ndi_input/ndi_input.cpp` (exists)
  - `source/stats/stats.cppm` (89 lines, NOT compiled) vs `source/stats/stats.cpp` (78 lines)
  - `source/lib/lib.cppm` (111 lines, NOT compiled) vs `source/lib/lib.cpp` (8 lines)
- **Impact:** Developers following AGENTS.md documentation about modules will be confused. Code changes to `.cppm` files will have no effect on the build. Maintenance burden of keeping two copies of code in sync.
- **Fix approach:** Either fully adopt C++20 modules (rebuild CMakeLists.txt with `enable_language(CXX)` and module compiler flags), or remove all `.cppm` files and use traditional `#include` headers.

### Tech Debt 2: Triple Copy of Type Definitions
- **Issue:** Type definitions (`input_mode`, `codec`, `encoder`, `encode_config`, `output_config`, etc.) exist in three different files with slight inconsistencies:
  - `include/common.h` — deprecated, all source files comment out `#include "common.h"`
  - `source/common.h` — also not included by any source file
  - `source/lib/lib.h` — the actual header used by all modules
- **Impact:** Risk of divergence. `source/common.h` is missing `mpegts` and `none` variants of `input_mode`. `include/common.h` has `BufferData` (capitalized) vs `buffer_data` (lowercase in `lib.h`). Adding a new enum value requires updating multiple files.
- **Files:** `include/common.h`, `source/common.h`, `source/lib/lib.h`
- **Fix approach:** Remove `include/common.h` and `source/common.h`. Use only `source/lib/lib.h` as the single source of truth.

### Tech Debt 3: Global Mutable State in `main.cpp`
- **Issue:** Four global variables (`app`, `transporter`, `ui`, `ptr_encoder`) in `source/main.cpp` create implicit coupling between all modules and make testing impossible.
- **Files:** `source/main.cpp` (lines 18-21, 33)
- **Impact:** No unit testing possible. Race conditions between global initialization order. Hard to reason about data flow.
- **Fix approach:** Pass dependencies explicitly through functions. Use dependency injection pattern. Move `library app` into a local variable in `main()`.

### Tech Debt 4: `encode* ptr_encoder` Is a Dangling Pointer Risk
- **Issue:** `ptr_encoder` (line 21 of `main.cpp`) is a raw `encode*` pointing to a stack-local `encode` object created in `run_loop()` (line 60). If `rist_stats_cb` is called before `ptr_encoder` is assigned (line 63) or after `run_loop` returns (encoder goes out of scope), this is a null pointer dereference or use-after-free.
- **Files:** `source/main.cpp` (lines 21, 50-55, 57-73)
- **Impact:** Crash on RIST statistics callback during startup or shutdown.
- **Fix approach:** Use `std::shared_ptr<encode>` or pass the encoder instance through callbacks. Never store a raw pointer to a stack-local object.

### Tech Debt 5: `atomic_bool` Initialization Is Problematic
- **Issue:** `is_running {std::atomic<bool>(false)}` in `lib.cpp` (line 4) and `lib.cppm` (line 106) attempts to copy-initialize an `std::atomic_bool` from a temporary `std::atomic<bool>`. Since `std::atomic` copy constructors are deleted, this is undefined behavior or a compile error depending on the compiler.
- **Files:** `source/lib/lib.cpp` (line 4), `source/lib/lib.cppm` (line 106)
- **Impact:** Undefined behavior at startup. The `is_running` value may not be initialized to `false`.
- **Fix approach:** Change to `is_running{false}` or `is_running(false)`.

### Tech Debt 6: Bitrate Stored as `std::string`
- **Issue:** `encode_config::bitrate` is `std::string` (lib.cppm line 65), not `int`. Every access requires `std::stoi()` which can throw `std::invalid_argument` or `std::out_of_range`. This happens in `stats::got_rist_statistics()` (line 22 of stats.cppm).
- **Files:** `source/lib/lib.cppm` (line 65), `source/lib/lib.h` (line 62), `source/stats/stats.cppm` (line 22)
- **Impact:** Application crash if user enters non-numeric bitrate.
- **Fix approach:** Change to `int bitrate = 4300` throughout. Parse to int once on input, store as int.

### Tech Debt 7: Output Config Fields All Stored as `std::string`
- **Issue:** All RIST output parameters (`buffer_min`, `buffer_max`, `rtt_min`, `rtt_max`, `reorder_buffer`, `bandwidth`) are `std::string` instead of `int`.
- **Files:** `source/lib/lib.cppm` (lines 71-76), `source/lib/lib.h` (lines 69-73)
- **Impact:** Same as debt #6 — `std::stoi()` calls scattered throughout transport code. No type safety.
- **Fix approach:** Change all to `int` with proper defaults.

## Performance Bottlenecks

### Bottleneck 1: Cumulative Averages Recalculate O(n) on Every Callback
- **Problem:** `stats::got_rist_statistics()` recalculates bandwidth_avg, encode_bitrate_avg, retransmitted_packets_sum, and total_packets_sum by iterating over the entire vector each time statistics arrive. As the program runs, vectors grow unbounded, making each callback slower.
- **Files:** `source/ndi_input/ndi_input.cppm` (lines 54-63)
- **Measurement:** After 10 minutes of operation (~600 callbacks at 1s interval), each callback does ~600 additions for each of 4 metrics. After 1 hour, ~3600 additions.
- **Suspected cause:** Using `std::accumulate` on growing vectors instead of online averaging.
- **Improvement path:** Replace with online averaging (keep running sum and count), or use a circular buffer with fixed size.

### Bottleneck 2: NDI Device Monitor Thread Spins With 1-Second Sleep
- **Problem:** The device monitor thread runs a loop with `sleep_for(1s)` but does nothing inside the loop (no processing, no event handling). The GStreamer device monitor bus is obtained but commented out.
- **Files:** `source/ndi_input/ndi_input.cppm` (lines 47-64)
- **Suspected cause:** Incomplete implementation. The thread was intended to process device events but the event loop is missing.
- **Improvement path:** Use `g_main_loop` to run the GStreamer device monitor bus events properly.

### Bottleneck 3: Unbounded Vector Growth in `cumulative_stats`
- **Problem:** The `bandwidth`, `retransmitted_packets`, `total_packets`, and `encode_bitrate` vectors in `cumulative_stats` grow without limit. Each RIST statistics callback (every ~1 second) pushes a new value. Over hours of operation, these vectors consume increasing memory.
- **Files:** `source/lib/lib.cppm` (lines 45-48), `source/ndi_input/ndi_input.cppm` (lines 49-52)
- **Impact:** Memory bloat over long-running sessions. Average calculations become slower.
- **Improvement path:** Cap vectors at a reasonable size (e.g., 3600 entries = 1 hour of data) and remove oldest entries.

## Fragile Areas

### Fragile Area 1: FLTK Grid Layout with Magic Indices
- **Why fragile:** Grid cell assignments use hardcoded child indices (`grid_stats->child(0)`, `child(16)`, `child(17)`) with no documentation of what each maps to. Adding/removing widgets will break all subsequent indices.
- **Files:** `source/ui/ui.cppm` (lines 447-489), `source/ui/ui.cpp` (lines 360-402)
- **Safe modification:** Use named widgets or a map of widget-to-position. Store indices in named constants.
- **Test coverage:** None. Widget indices are purely visual and cannot be unit tested.

### Fragile Area 2: Menu Choice User Data Uses Magic Numbers
- **Why fragile:** Input protocol menu items store integer user data (1=SDP, 2=NDI, 3=MPEGTS) rather than enum values. The UI callback switches on these integers (ui.cppm lines 572-621), which can silently mismatch if enum values change.
- **Files:** `source/ui/ui.cppm` (lines 106-151, 572-621), `source/ui/ui.cpp` (lines 19-70, 483-535)
- **Safe modification:** Store actual `input_mode` enum values as user_data (cast to `void*`) instead of magic integers. Update switch cases accordingly.

### Fragile Area 3: Listen Port Input Field Interprets Port as IP Address
- **Why fragile:** The `input_listen_port` Fl_Input is labeled "Listen Port" but its callback (`input_listen_port_cb`) stores the value in `input_config->selected_input` which is a string used as a UDP port number in `pipeline_build_source()`. The pipeline format string `udpsrc port={}` expects a numeric port, but `selected_input` is typed as `std::string`.
- **Files:** `source/ui/ui.cppm` (lines 551-554), `source/encode/encode.cppm` (lines 125-128)
- **Safe modification:** Add a separate `int listen_port` field to `input_config`. Validate numeric input.

### Fragile Area 4: Audio Sink Pipeline String Has Inconsistent Naming
- **Why fragile:** `pipeline_build_sink()` creates `appsink name=video_sink` but the pipeline also references `! video_sink.` as a target for the mpegtsmux output. The audio path is commented out but the demux paths reference both `demux.video` and `demux.audio`. Any change to the video sink name requires updating all references.
- **Files:** `source/encode/encode.cppm` (lines 147-153, 159, 408), `source/encode/encode.cpp` (lines 75-81, 87, 336)
- **Safe modification:** Use named constants for pipeline element names.

### Fragile Area 5: Transport Module Has Duplicate `.cpp` and `.cppm` with Different Code
- **Why fragile:** `transport.cpp` and `transport.cppm` contain similar but NOT identical code. For example, `transport.cpp` uses `#include "transport/transport.h"` while `transport.cppm` uses `import library;`. If changes are applied to only one file, the compiled code diverges from the documented code.
- **Files:** `source/transport/transport.cpp` vs `source/transport/transport.cppm`
- **Safe modification:** Consolidate to a single implementation approach. Either use modules properly or remove `.cppm` files.

## Dependency Risks

### Risk 1: GStreamer 1.28+ Requirement Is Very Recent
- **Risk:** CMakeLists.txt requires `gstreamer-1.0>=1.28` and related plugins at 1.28+. This is a very recent version (as of early 2026). Many Linux distributions ship GStreamer 1.24 or 1.26.
- **Impact:** Build will fail on older distributions (Ubuntu 22.04, Debian 12, Fedora 39). Users must compile from source or use newer distros.
- **Mitigation:** Document minimum OS requirements. Consider adding a fallback to detect available GStreamer version and adapt.

### Risk 2: NDI SDK Requires Separate Installation and Has Licensing Restrictions
- **Risk:** Newtek NDI SDK must be installed separately (`find_package(NDI REQUIRED)` in CMakeLists.txt line 29). The SDK has a free "Development" license and a commercial license. Distribution of binaries requires proper licensing.
- **Impact:** Cannot build without NDI SDK. Cannot distribute binaries without checking licensing terms.
- **Mitigation:** Document NDI installation steps. Consider making NDI support optional via CMake option.

### Risk 3: rist-cpp Forked to Personal Repository
- **Risk:** The rist-cpp submodule points to `patcarter883/rist-cpp` (personal fork) rather than the upstream repository. Personal forks may become unavailable if the owner stops maintaining them.
- **Files:** `.gitmodules` (line 8-9)
- **Impact:** Build fails if the personal fork is deleted or becomes private.
- **Mitigation:** Contribute changes upstream or maintain a fork on an organization account. Pin to a specific commit hash in `.gitmodules`.

### Risk 4: FLTK Submodule From Official Repository
- **Risk:** FLTK is a git submodule from the official repository. The version is not pinned to a specific release tag.
- **Files:** `.gitmodules` (line 4-6)
- **Impact:** `git submodule update` may pull a breaking change from the main branch.
- **Mitigation:** Pin submodule to a specific release tag (e.g., `fltk-1.4.0`).

### Risk 5: fmt Library Version Requirement
- **Risk:** vcp.json requires `fmt >= 11.0.2`. The project uses `std::format` throughout, which may use `fmt` as a replacement provider. If fmt is not available, `std::format` may not compile.
- **Files:** `vcpkg.json` (line 7)
- **Impact:** Build failure if fmt 11+ is not available via vcpkg.
- **Mitigation:** The vcpkg baseline is pinned (good practice). Ensure CI uses the same baseline.

## Missing Critical Features (If Any)

### Gap 1: No Input Validation or Error Dialogs
- **Problem:** If GStreamer pipeline fails to parse, the error is only logged. The user has no visual indication that the pipeline failed, and the UI continues to show "encoding" state.
- **Blocks:** Any change-routing work that adds new input types or encoder configurations, since there is no mechanism to report errors to the user.

### Gap 2: No Stop/Cleanup on Pipeline Failure
- **Problem:** When `handle_gst_message_error()` is called (encode.cppm line 500), it sets `encoder_running = false` but does not tear down the pipeline, stop the transport, or update the UI. The application may be left in a partially-running state.
- **Blocks:** Any robustness improvements to the encoder pipeline.

### Gap 3: No Persistent Configuration
- **Problem:** All settings (address, bitrate, codec, encoder) are lost on application restart. There is no save/load configuration mechanism.
- **Blocks:** Any production deployment scenario where operators need to configure and reuse settings.

## Test Coverage Gaps

### Gap 1: Zero Tests for Core Encoding Pipeline
- **What's not tested:** GStreamer pipeline construction for all encoder+codec combinations (4 encoders × 3 codecs = 12 configurations). The `build_pipeline()`, `pipeline_build_source()`, and all `pipeline_build_*_encoder()` methods have no tests.
- **Files:** `source/encode/encode.cppm`, `source/encode/encode.cpp`
- **Risk:** Bug 1 (NVENC AV1 wrong encoder), Bug 2 (H265 wrong parser), and Bug 5 (audio sink) would have been caught by unit tests.
- **Priority:** High

### Gap 2: Zero Tests for Transport Layer
- **What's not tested:** RIST URL construction, multiple stream port offset calculation, buffer sending. The `setup_rist_sender()` method which contains Bug 3 (missing `&`) has no tests.
- **Files:** `source/transport/transport.cppm`, `source/transport/transport.cpp`
- **Risk:** URL construction bugs go undetected. Changes to `output_config` format silently break transport.
- **Priority:** High

### Gap 3: Zero Tests for Adaptive Bitrate Logic
- **What's not tested:** The bitrate adjustment algorithm in `stats::got_rist_statistics()`. Quality drop detection, gradual increase, rate limiting, and bounds clamping are never verified.
- **Files:** `source/ndi_input/ndi_input.cppm` (lines 17-89), `source/ndi_input/ndi_input.cpp`
- **Risk:** Bitrate may increase beyond network capacity (causing packet loss) or decrease too aggressively (poor quality).
- **Priority:** High

### Gap 4: Zero Tests for NDI Module
- **What's not tested:** Device discovery, device name memory management, preview pipeline construction. The memory leak in `refresh_devices()` (gst_device_get_display_name never freed) and the main loop leak in `preview()` have no tests.
- **Files:** `source/ndi_input/ndi_input.cppm`, `source/ndi_input/ndi_input.cpp`
- **Risk:** Memory leaks accumulate during operation. Preview crashes silently.
- **Priority:** Medium

### Gap 5: Zero Tests for UI Module
- **What's not tested:** Callback binding, menu selection, log append thread safety. The missing FLTK lock in `transport_log_append()` and `encode_log_append()` (ui.cppm lines 443-458) cannot be detected by tests.
- **Files:** `source/ui/ui.cppm`, `source/ui/ui.cpp`
- **Risk:** UI corruption from concurrent thread access. Null pointer dereference in log append.
- **Priority:** High

### Gap 6: Only 1 Test Exists and It Tests a Trivial Fact
- **What's tested:** `library.name == "open-broadcast-encoder"` (test CMakeLists.txt + source/test).
- **What's NOT tested:** Any functionality. Zero code paths are covered.
- **Files:** `test/source/open-broadcast-encoder_test.cpp`
- **Risk:** Any change can silently break functionality with no regression detection.
- **Priority:** Critical

### Gap 7: Test Target Disabled in Build System
- **What's not tested:** The entire test suite is commented out in `cmake/dev-mode.cmake` (lines 3-6). Even if tests were written, they would not run.
- **Files:** `cmake/dev-mode.cmake` (lines 3-6)
- **Risk:** Developers cannot run tests even if they exist. CI/CD has no test stage.
- **Priority:** High

## Downstream Impact Ranking

The following concerns are ranked by how much future work they block, using the heuristic that concerns blocking multiple change-routing rows rank highest.

| Rank | Concern | Blocks | Severity | Fix effort |
|------|---------|--------|----------|------------|
| 1 | Test coverage gap (Gap 7: test target disabled + Gap 6: only 1 trivial test) | All change-routing rows — any new feature, bug fix, or refactor has no regression safety net. Blocks safe development of the entire codebase. | critical | medium |
| 2 | Dead code: `.cppm` files not compiled (Tech Debt 1) | Adding new encoders, codecs, input types, and transport configurations. Developers following AGENTS.md documentation will write to `.cppm` files that have no effect on the build. Blocks all change-routing rows that add new pipeline configurations. | moderate | small |
| 3 | Triple copy of type definitions (Tech Debt 2) | Adding new enum values, config options, or data structures. Requires updates to 3 files with risk of divergence. Blocks all change-routing rows that modify shared types (encoder selection, input protocol changes, config extensions). | moderate | small |
| 4 | Global mutable state (Tech Debt 4) | Any change-routing row that needs to modify encoder behavior, transport settings, or UI state in isolation. Makes it impossible to test or reason about component interactions. Blocks all cross-component change-routing rows. | moderate | large |
| 5 | H.265 encoder wrong parser (Bug 2) | H.265 encoding with AMD, QSV, or software encoder. Any future work that adds H.265-specific features or optimizations is built on broken foundations. | critical | small |

---

*Concerns audit: 2026-04-28*
