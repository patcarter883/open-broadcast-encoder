# 008-SUMMARY.md

## Completed

Added `testsrc` input mode that generates video via GStreamer's `videotestsrc`
(smptebars pattern) and audio via `audiotestsrc` (silence).

### Changes

**`source/lib/lib.h`**
- Added `testsrc` as first enum value in `input_mode` (index 0)

**`source/ui/ui.cpp`**
- Added "Test Source" menu item with `user_data_ = 0`
- Reordered menu: Test Source (0), MPEGTS (1), SDP (2), NDI (3)
- Updated `select_*_input` pointers to match new indices
- Added `case 0` to `choose_input_protocol()`: sets mode to `testsrc`, hides all option groups

**`source/encode/encode.cpp`**
- Added `case input_mode::testsrc` to `pipeline_build_source()`:
  Generates `audiotestsrc ! audioconvert ! videotestsrc pattern=smptebars ! videoconvert !`
- Added `case input_mode::testsrc` to `pipeline_build_video_demux()`:
  Appends ` ! ` to chain with encoder
- Added `case input_mode::testsrc` to `pipeline_build_audio_demux()`:
  Generates ` ! avenc_aac ! aacparse ! tsmux. ` to route audio to mux

### Verification

- Build completed successfully (`cmake --build build`)
- Code formatted (`cmake --build build -t format-fix`)
