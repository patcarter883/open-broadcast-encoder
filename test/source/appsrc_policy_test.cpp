// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The preview and the encode path read the same OBS socket but must not share a
// back-pressure policy: a stalled preview sink may drop frames, a stalled
// encoder may not. These tests pin both policies as pipeline properties,
// because a preview appsrc that blocks stops the reader draining the socket and
// OBS gives up after 90 frames ("peer unreachable").

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

TEST_CASE("the preview video queue holds a handful of frames, not two",
          "[encode][preview]")
{
  // max-bytes=16777216 was two 1080p BGRA frames: any hiccup filled it, and the
  // buffer cap -- not the frame count -- was what the queue was sized by.
  CHECK(appsrc_policy::k_preview_video_max_buffers >= 6);
  CHECK(appsrc_policy::k_preview_video_max_bytes
        >= 6 * appsrc_policy::k_frame_1080p_bgra);
  CHECK(appsrc_policy::k_frame_1080p_bgra == 8294400ull);
  // The frame count is the binding limit, so the byte cap cannot shrink it.
  CHECK(appsrc_policy::k_preview_video_max_bytes
        >= appsrc_policy::k_preview_video_max_buffers
            * appsrc_policy::k_frame_1080p_bgra);
  CHECK(contains(
      appsrc_policy::preview_video_src(),
      "max-buffers="
          + std::to_string(appsrc_policy::k_preview_video_max_buffers)));
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
