# 008-PLAN.md

## Objective

Add a `testsrc` input mode that generates video via GStreamer's `videotestsrc`
(smptebars pattern) and audio via `audiotestsrc` (silence), replacing the need
for external SDP/NDI/MPEGTS sources.

## Tasks

### Task 1: Add `testsrc` to `input_mode` enum

**Files:** `source/lib/lib.h`

- Add `testsrc` as the first enum value (index 0) in `enum class input_mode`

### Task 2: Update UI menu and callback

**Files:** `source/ui/ui.cpp`

- Update `menu_choice_input_protocol` array:
  - Add "Test Source" entry with `user_data_ = 0` (maps to `input_mode::testsrc`)
  - Update remaining entries' user_data: MPEGTS=1, SDP=2, NDI=3
- Update selection pointers: `select_mpegts_input`, `select_sdp_input`, `select_ndi_input`
- Add case `0` to `choose_input_protocol()`: set mode to `testsrc`, hide all option groups (mpegts, sdp, ndi)

### Task 3: Implement test source pipeline in encode module

**Files:** `source/encode/encode.cpp`

Add `case input_mode::testsrc` to:

**`pipeline_build_source()`**
```cpp
case input_mode::testsrc:
  this->pipeline_str =
      "audiotestsrc is-live=true ! audioconvert ! "
      "videotestsrc pattern=smptebars ! videoconvert ! ";
  break;
```

**`pipeline_build_video_demux()`**
```cpp
case input_mode::testsrc:
  this->pipeline_str += " ! ";
  break;
```

**`pipeline_build_audio_demux()`**
```cpp
case input_mode::testsrc:
  this->pipeline_str += " ! avenc_aac ! aacparse ! tsmux. ";
  break;
```

**How it works:** The test source pipeline generates both audio and video
concurrently. Audio goes through `audioconvert → avenc_aac → aacparse → tsmux`.
Video goes through `videoconvert → encoder (added by pipeline_build_video_encoder())
→ tsmux`. Both streams converge at the tsmux element.
