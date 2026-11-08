// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The preview and the encode path read the same OBS socket but must not share a
// back-pressure policy: a stalled preview sink may drop frames, a stalled
// encoder may not. These tests pin both policies as pipeline properties,
// because a preview appsrc that blocks stops the reader draining the socket and
// OBS gives up after 90 frames ("peer unreachable"), and they pin the preview
// queue's byte cap as a function of the negotiated caps rather than a constant.

#include <string>

#include "encode/appsrc_policy.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
bool contains(const std::string& haystack, const std::string& needle)
{
  return haystack.find(needle) != std::string::npos;
}
}  // namespace

TEST_CASE("the preview appsrcs never block and drop their oldest frame",
          "[encode][preview]")
{
  for (const std::string& src :
       {appsrc_policy::preview_video_src(), appsrc_policy::preview_audio_src()})
  {
    REQUIRE(contains(src, "block=false"));
    REQUIRE(contains(src, "leaky-type=downstream"));
  }
}

TEST_CASE(
    "the encode appsrcs keep blocking so OBS, not the queue, is the loss "
    "point",
    "[encode][raw]")
{
  for (const std::string& src :
       {appsrc_policy::encode_video_src(), appsrc_policy::encode_audio_src()})
  {
    REQUIRE(contains(src, "block=true"));
    // Lossless by statement, not by inheritance from a default.
    REQUIRE(contains(src, "leaky-type=none"));
    CHECK_FALSE(contains(src, "leaky-type=downstream"));
  }
}

TEST_CASE("a frame's size follows the negotiated format, not the name alone",
          "[encode][preview]")
{
  // 4:2:0 is 12/8 of a byte per pixel; P010 carries two planes of 16-bit
  // samples, so three; packed 4:2:2 is two; BGRA is four.
  CHECK(appsrc_policy::preview_frame_bytes("NV12", 1920, 1080)
        == 1920ull * 1080ull * 3ull / 2ull);
  CHECK(appsrc_policy::preview_frame_bytes("I420", 1920, 1080)
        == 1920ull * 1080ull * 3ull / 2ull);
  CHECK(appsrc_policy::preview_frame_bytes("P010_10LE", 1920, 1080)
        == 1920ull * 1080ull * 3ull);
  CHECK(appsrc_policy::preview_frame_bytes("YUY2", 1920, 1080)
        == 1920ull * 1080ull * 2ull);
  CHECK(appsrc_policy::preview_frame_bytes("GRAY8", 1920, 1080)
        == 1920ull * 1080ull);
  CHECK(appsrc_policy::preview_frame_bytes("BGRA", 1920, 1080)
        == appsrc_policy::k_frame_1080p_bgra);
  CHECK(appsrc_policy::k_frame_1080p_bgra == 8294400ull);
}

TEST_CASE("the preview queue holds a frame count of the real frame size",
          "[encode][preview]")
{
  // max-bytes=16777216 was about two 1080p BGRA frames: any hiccup filled it,
  // and at 1440p the same constant held under two frames. The cap now follows
  // the caps, so the depth is a frame count at every size.
  const std::uint64_t full_hd =
      appsrc_policy::preview_video_max_bytes("BGRA", 1920, 1080);
  CHECK(full_hd
        == appsrc_policy::k_preview_video_frames
            * appsrc_policy::k_frame_1080p_bgra);
  CHECK(full_hd > 16777216ull);

  const std::uint64_t qhd =
      appsrc_policy::preview_video_max_bytes("BGRA", 2560, 1440);
  CHECK(qhd
        == appsrc_policy::k_preview_video_frames * 2560ull * 1440ull * 4ull);
  CHECK(qhd > full_hd);
}

TEST_CASE("the preview queue is bounded at both ends", "[encode][preview]")
{
  // A tiny source must not end up with a queue shallower than the fixed value
  // it replaced: the frame count is already small there, and the floor keeps
  // the slack the old pipeline had.
  CHECK(appsrc_policy::preview_video_max_bytes("GRAY8", 320, 240)
        == appsrc_policy::k_preview_video_floor_bytes);
  CHECK(appsrc_policy::preview_video_max_bytes("NV12", 640, 360)
        >= appsrc_policy::k_preview_video_floor_bytes);

  // A 4K BGRA frame is ~33 MB, and a frame count of those is a couple of
  // hundred megabytes held for a preview. Dropping preview frames is preferred
  // to reserving that much.
  CHECK(appsrc_policy::preview_video_max_bytes("BGRA", 3840, 2160)
        == appsrc_policy::k_preview_video_ceiling_bytes);
  CHECK(appsrc_policy::k_preview_video_ceiling_bytes
        < appsrc_policy::k_preview_video_frames * 3840ull * 2160ull * 4ull);
}

TEST_CASE("the preview appsrc carries the frame count as its buffer cap",
          "[encode][preview]")
{
  CHECK(contains(
      appsrc_policy::preview_video_src(),
      "max-buffers=" + std::to_string(appsrc_policy::k_preview_video_frames)));
  // The byte cap in the element string is the pre-caps floor; the reader
  // restates it from the negotiated caps once they arrive.
  CHECK(contains(
      appsrc_policy::preview_video_src(),
      "max-bytes="
          + std::to_string(appsrc_policy::k_preview_video_floor_bytes)));
}

TEST_CASE("the two policies are distinct appsrc elements", "[encode][preview]")
{
  // Same names, because the reader looks them up by name; what separates the
  // two paths is that they are built into different pipelines and fed by
  // different readers, never that one overrides the other's properties.
  CHECK(contains(appsrc_policy::preview_video_src(), "name=rawvideo"));
  CHECK(contains(appsrc_policy::encode_video_src(), "name=rawvideo"));
  CHECK(appsrc_policy::preview_video_src()
        != appsrc_policy::encode_video_src());
}
