# Quick Task Plan: Fix encode bugs and consolidate type definitions

## Objective
Fix 6 identified bugs across encode/transport/ndi modules and remove duplicate type definitions to use `source/lib/lib.cppm` as the canonical source.

## Task 1: Fix all 6 encode bugs
**Description:** Correct codec/parsing element mismatches, memory leaks, missing unmapping, and audio sink plumbing across encode, transport, and ndi_input modules.
**Files:**
- `source/encode/encode.cppm`
- `source/transport/transport.cppm`
- `source/transport/transport.cpp`
- `source/ndi_input/ndi_input.cppm`

**Action:**

1. **Bug 1 — NVENC AV1 uses wrong element** (`encode.cppm:355-358`):
   Replace the entire `pipeline_build_nvenc_av1_encoder()` body with:
   ```
   this->pipeline_str += std::format(
       "nvav1enc name=videncoder bitrate={} rate-control=cbr "
       "preset=low-latency-hq ! av1parse ",
       encode_c.bitrate);
   ```
   (Change `x264enc` → `nvav1enc`, `h264parse` → `av1parse`, fix speed/tune params for nvenc)

2. **Bug 2 — H265 encoders use wrong parser** (3 locations):
   - `encode.cppm:297` — `pipeline_build_amd_h265_encoder()`: change `h264parse` to `h265parse`
   - `encode.cppm:324` — `pipeline_build_qsv_h265_encoder()`: change `h264parse` to `h265parse`
   - `encode.cppm:373` — `pipeline_build_software_h265_encoder()`: change `h264parse` to `h265parse`

3. **Bug 3 — RIST URL missing `&` separator** (`transport.cppm:94`):
   Change `"{}:{}?bandwidth={}buffer-min={}"` to `"{}:{}?bandwidth={}&buffer-min={}"` (add `&` after `{}` for bandwidth).
   Check `transport.cpp` for the same pattern and fix if present.

4. **Bug 4 — `pull_video_buffer()` missing `gst_buffer_unmap()`** (`encode.cppm:536-550`):
   After `gst_sample_unref(sample)` at line 544, add `gst_buffer_unmap(buffer, &info)` before the return statement at line 545.
   Also add `gst_buffer_unmap(buffer, &info)` in `pull_audio_buffer()` after line 560.

5. **Bug 5 — Audio sink never created** (`encode.cppm:147-153`):
   Uncomment and enable the audio sink in `pipeline_build_sink()`:
   ```
   this->pipeline_str +=
       " appsink name=audio_sink "
       " appsink name=video_sink "
       "mpegtsmux alignment=7 name=tsmux ! video_sink. ";
   ```

6. **Bug 6 — NDI main loop leak** (`ndi_input.cppm:134-141`):
   After line 140 (`gst_object_unref(pipeline)`), add `g_main_loop_destroy(loop);` before the closing brace of the lambda.

**Verify:**
```bash
# Build to confirm no compile errors
cmake --preset=dev --fresh 2>&1 | tail -5 && cmake --build --preset=dev 2>&1 | tail -10

# Verify specific fixes via grep
rg 'nvav1enc' source/encode/encode.cppm                    # Bug 1: NVENC AV1 uses nvav1enc
rg 'h265parse' source/encode/encode.cppm                   # Bug 2: h265parse in all 3 h265 encoder functions
rg 'bandwidth=\{\}&buffer-min' source/transport/transport.* # Bug 3: & separator present
rg 'gst_buffer_unmap' source/encode/encode.cppm            # Bug 4: unmapping present
rg 'audio_sink' source/encode/encode.cppm                  # Bug 5: audio_sink in pipeline
rg 'g_main_loop_destroy' source/ndi_input/ndi_input.cppm   # Bug 6: loop destroyed
```

## Task 2: Consolidate type definitions
**Description:** Remove duplicate type definitions from `include/common.h` and `source/common.h`. The canonical source is `source/lib/lib.cppm` (the exported `library` module). All source files already `import library;` which provides these types.
**Files:**
- `include/common.h` — DELETE
- `source/common.h` — DELETE
- `source/ui/ui.fld` — update `#include "common.h"` reference

**Action:**

1. Delete `include/common.h` (incomplete `input_mode` without `mpegts`, wrong `BufferData` casing).

2. Delete `source/common.h` (incomplete `buffer_data` without `seq`/`ts_ntp`, `input_config` without `selected_input_mode`).

3. Verify no files still reference these headers by searching for `#include "common.h"` — only the `// #include "common.h"` comment-out lines should remain in `.cppm` files (already inert).

4. Check `source/ui/ui.fld` line 5 (`decl {#include "common.h"}`) — update to `#include "lib/lib.h"` since FLTK code-generation files need the non-module header.

5. Verify `source/lib/lib.h` remains intact (it's the non-module header that `.h` files include and `ui.fld` needs).

**Verify:**
```bash
# Confirm deleted files are gone
test ! -f include/common.h && echo "include/common.h removed OK" || echo "STILL EXISTS"
test ! -f source/common.h && echo "source/common.h removed OK" || echo "STILL EXISTS"

# Verify no active includes of deleted headers remain
rg '#include "common\.h"' source/ --no-comments || echo "No active includes found (good)"

# Verify lib.h still exists and has complete types
rg 'mpegts' source/lib/lib.h && echo "lib.h has mpegts (good)"
rg 'ts_ntp' source/lib/lib.h && echo "lib.h has ts_ntp (good)"
rg 'selected_input_mode' source/lib/lib.h && echo "lib.h has selected_input_mode (good)"

# Full build test after consolidation
cmake --build --preset=dev 2>&1 | tail -10
```
