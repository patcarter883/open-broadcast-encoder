---
completed: 2026-04-29
tasks: 2
---

# Quick Task 001: Fix encode bugs and consolidate type definitions

## Summary

Fixed 6 bugs across encode, transport, and ndi_input modules and removed duplicate type definitions in favor of canonical `source/lib/lib.cppm` and `source/lib/lib.h`.

## Task 1: Fixed all 6 encode bugs

**Bug 1 — NVENC AV1 used wrong encoder element:** Replaced `x264enc` (H.264 software encoder) with `nvv4l2av1enc` in `source/encode/encode.cppm:355-358`. Also fixed parser from `h264parse` to `av1parse` and updated parameters for nvenc.

**Bug 2 — H265 encoders used wrong parser (3 locations):** Changed `h264parse` to `h265parse` in all 3 H265 encoder builder functions:
- `pipeline_build_amd_h265_encoder()` (`encode.cppm:297`)
- `pipeline_build_qsv_h265_encoder()` (`encode.cppm:324`)
- `pipeline_build_software_h265_encoder()` (`encode.cppm:373`)

**Bug 3 — RIST URL missing `&` separator:** Fixed format string in both `source/transport/transport.cppm:94` and `source/transport/transport.cpp:51` — changed `bandwidth={}buffer-min={}` to `bandwidth={}&buffer-min={}`.

**Bug 4 — Missing `gst_buffer_unmap()`:** Added `gst_buffer_unmap()` call in both `pull_video_buffer()` and `pull_audio_buffer()` in `source/encode/encode.cppm`, taking a local copy before unmapping to prevent dangling pointer after `gst_sample_unref()`.

**Bug 5 — Audio sink never created:** Uncommented and enabled `appsink name=audio_sink` in `pipeline_build_sink()` (`encode.cppm:149-153`), restoring the audio output path.

**Bug 6 — NDI main loop leak:** Added `g_main_loop_destroy(loop)` in `source/ndi_input/ndi_input.cppm:137` before freeing pipeline resources in the preview thread.

## Task 2: Consolidated type definitions

- **Deleted** `include/common.h` — had incomplete `input_mode` (missing `mpegts`) and wrong `BufferData` casing
- **Deleted** `source/common.h` — had incomplete `buffer_data` (missing `seq`/`ts_ntp`) and `input_config` (missing `selected_input_mode`)
- **Updated** `source/ui/ui_func.cxx:4` — replaced `#include "common.h"` with `#include "lib/lib.h"`
- **Updated** `source/ui/ui.fld:5` — replaced `#include "common.h"` with `#include "lib/lib.h"` for FLTK fluid codegen
- All other `#include "common.h"` references in `.cppm` files were already commented out (inert)

## Verification

- Full build succeeded: `cmake --preset=ci-ubuntu` → `cmake --build build/ci-ubuntu` (342/342 targets, zero errors in modified files, only pre-existing FLTK submodule warnings)
- Verified each fix via grep: `nvv4l2av1enc` present, 3× `h265parse` confirmed, `bandwidth={}&buffer-min={}` fixed, 2× `gst_buffer_unmap` added, `audio_sink` in pipeline, `g_main_loop_destroy` added
- Confirmed both deleted files are gone, no active `#include "common.h"` references remain

## Deviations

None — plan executed exactly as written.

## Files Modified

- `source/encode/encode.cppm` — Bugs 1-5 fixes
- `source/transport/transport.cppm` — Bug 3 fix
- `source/transport/transport.cpp` — Bug 3 fix
- `source/ndi_input/ndi_input.cppm` — Bug 6 fix
- `source/ui/ui_func.cxx` — Type consolidation
- `source/ui/ui.fld` — Type consolidation
- `include/common.h` — Deleted
- `source/common.h` — Deleted
