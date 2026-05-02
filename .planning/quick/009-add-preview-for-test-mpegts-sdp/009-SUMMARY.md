# Quick Task 009 Summary

## Description
Add preview input support for test source, mpegts, and sdp input modes.

## Changes Made

### source/main.cpp
- Added `#include <gst/video/video.h>`, `<fstream>`, `<filesystem>` for preview functionality
- Added `run_preview_pipeline()` helper function that:
  - Parses and launches a GStreamer pipeline
  - Sets it to PLAYING state
  - Waits for ERROR or EOS messages on the bus
  - Logs errors and cleans up resources
- Expanded `preview_input()` to handle all input modes:
  - **testsrc**: videotestsmptebars + audiotestsrc with autovideosink/autoaudiosink
  - **mpegts**: udpsrc with configurable port, tsdemux with video+audio routing
  - **sdp**: sdpsrc with temp file containing SDP string, rtpvrawdepay
  - **ndi**: existing ndi->preview() call (unchanged)

### source/ndi_input/ndi_input.cpp
- Fixed preview hang bug: removed blocking `g_main_loop_run()` call
- Preview now properly returns after ERROR/EOS handling
- Resource cleanup (bus unref, pipeline state NULL, pipeline unref) preserved

## Build Status
- Build: SUCCESS (no errors)
- Format: SUCCESS
