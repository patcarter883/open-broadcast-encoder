# Quick Task 001 Verification Report

**Task:** Fix encode bugs and consolidate type definitions
**Status: passed**

---

## Bug 1 — NVENC AV1 wrong encoder element: PASS

**Expected:** `nvv4l2av1enc` (or equivalent NVENC AV1 element) in `pipeline_build_nvenc_av1_encoder()`.

**Actual:** Line 357: `"nvv4l2av1enc name=videncoder bitrate={} rc-mode=cbr-hq ..."`
- Correct encoder: `nvv4l2av1enc` (modern NVIDIA V4L2 AV1 encoder)
- Correct parser: `av1parse` (line 358)
- Not using `x264enc` (H.264 software encoder) — bug fixed.

---

## Bug 2 — H265 encoders use wrong parser: PASS

**Expected:** All 3 H265 encoders use `h265parse`.

| Encoder | Line | Parser Used |
|---------|------|-------------|
| AMD H265 | 297 | `h265parse` |
| QSV H265 | 325 | `h265parse` |
| Software H265 | 374 | `h265parse` |

Remaining `h264parse` occurrences (5) are all in H264 encoder functions — correct. Bug fixed.

---

## Bug 3 — RIST URL missing `&` separator: PASS

**Expected:** `bandwidth={}&buffer-min={}` in both transport files.

- `transport.cppm:94`: `"{}:{}?bandwidth={}&buffer-min={}&buffer-max={}&rtt-min={}&rtt-max={}&"`
- `transport.cpp:51`: `"{}:{}?bandwidth={}&buffer-min={}&buffer-max={}&rtt-min={}&rtt-max={}&"`

Both files have the `&` separator. Bug fixed.

---

## Bug 4 — Missing `gst_buffer_unmap()`: PASS

**Expected:** `gst_buffer_unmap()` in both `pull_video_buffer()` and `pull_audio_buffer()`.

- `pull_video_buffer()`: Line 546 — `gst_buffer_unmap(buffer, &info)` called before `gst_sample_unref(sample)` at line 547.
- `pull_audio_buffer()`: Line 564 — `gst_buffer_unmap(buffer, &info)` called before `gst_sample_unref(sample)` at line 565.

Both properly map → copy data → unmap → unref. Bug fixed.

---

## Bug 5 — Audio sink never created: PASS

**Expected:** `appsink name=audio_sink` in `pipeline_build_sink()`.

- `encode.cppm:150`: `" appsink name=audio_sink "` present and active.

(Note: `encode.cpp:79` has a commented-out copy of this line, which is the auto-generated duplicate.) Bug fixed.

---

## Bug 6 — NDI main loop leak: PASS

**Expected:** `g_main_loop_destroy(loop)` in `ndi_input.cppm`.

**Actual:** Line 138: `g_main_loop_destroy(loop);` present in the preview thread cleanup lambda. Bug fixed.

---

## Task 2 — Type Consolidation: PASS

| Check | Result |
|-------|--------|
| `include/common.h` deleted | PASS — `glob **/common.h` returned no files |
| `source/common.h` deleted | PASS — confirmed by same glob |
| No active `#include "common.h"` in source | PASS — all 7 occurrences are `// #include "common.h"` (commented out) in `.cppm` files |
| `source/ui/ui.fld` updated to `#include "lib/lib.h"` | PASS — line 5: `decl {\#include "lib/lib.h"}` |
| `source/ui/ui_func.cxx` updated to `#include "lib/lib.h"` | PASS — line 5: `#include "lib/lib.h"` |
| `source/lib/lib.h` intact with complete types | PASS — exists, contains `mpegts`, `ts_ntp`, `selected_input_mode` |

---

## Build Verification: PASS

```
[28/28] Linking CXX executable open-broadcast-encoder
```

- Full build succeeded: 28/28 targets, 0 errors
- Warnings are only pre-existing FLTK submodule style warnings in `url.h`
- No compilation errors in any modified files

---

## Summary

All 6 bugs fixed, type consolidation complete, build passes with zero errors. No gaps found.
