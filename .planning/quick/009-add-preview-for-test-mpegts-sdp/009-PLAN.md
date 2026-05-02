# Quick Task 009: Add Preview Input Support

## Objective
Extend the existing "Preview Input" button to support test source, MPEGTS, and SDP input modes (NDI preview already works).

## Tasks

### Task 1: Add helper function and expand preview_input in main.cpp
- **Action**: Add `run_preview_pipeline()` helper and expand `preview_input()` to handle testsrc, mpegts, sdp
- **Files**: `source/main.cpp`
- **Details**:
  1. Add includes: `<gst/video/video.h>` and `<gst/sdp.h>`
  2. Add `run_preview_pipeline()` helper that builds a GStreamer preview pipeline with `autovideosink`, runs the bus loop for error/EOS, and cleans up
  3. Expand `preview_input()` switch to handle:
     - `testsrc`: `"audiotestsrc is-live=true ! audioconvert ! videotestsrc pattern=smptebars ! videoconvert ! autovideosink audiotestsrc is-live=true ! autoaudiosink"`
     - `mpegts`: `"udpsrc port={port} ! tsdemux name=d ! d.video ! queue ! videoconvert ! autovideosink d.audio ! queue ! audioconvert ! autoaudiosink"`
     - `sdp`: Build SDP string inline (same as encode.cpp), use `"sdpsrc sdp=\"{sdp}\" ! rtpvrawdepay ! videoconvert ! autovideosink"`
- **Verify**: `cmake --build build -t format-fix && cmake --build build 2>&1 | grep -i error || echo "Build OK"`

### Task 2: Fix NDI preview hang bug in ndi_input.cpp
- **Action**: Remove blocking `g_main_loop_run()` from `ndi_input::preview()`
- **Files**: `source/ndi_input/ndi_input.cpp`
- **Details**: The `g_main_loop_run(loop)` after error/EOS handling causes the preview to hang. Remove the main loop creation/run and just let the function return after cleanup.
- **Verify**: `cmake --build build -t format-fix && cmake --build build 2>&1 | grep -i error || echo "Build OK"`
