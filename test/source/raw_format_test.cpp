// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <string>

#include "encode/raw_format.h"

#include <catch2/catch_test_macros.hpp>

// The values are libobs' `enum video_format`
// (/usr/include/obs/media-io/video-io.h) and travel on the raw wire verbatim,
// so they are an external contract: if this fails, the plugin's wire and this
// reader have drifted apart.
TEST_CASE("obs format constants match the libobs enum", "[raw][format]")
{
  REQUIRE(kObsI420 == 1u);
  REQUIRE(kObsNv12 == 2u);
  REQUIRE(kObsYuy2 == 4u);
  REQUIRE(kObsY800 == 9u);
  REQUIRE(kObsP010 == 18u);
}

TEST_CASE("obs video formats map to GStreamer caps names", "[raw][format]")
{
  // The shipped default must not move.
  REQUIRE(std::string(obs_video_format_to_gst(kObsNv12)) == "NV12");

  // 10-bit 4:2:0: the higher-precision capture format. GStreamer's name for it
  // is P010_10LE, so this path needs no repack.
  REQUIRE(std::string(obs_video_format_to_gst(kObsP010)) == "P010_10LE");

  // 4:2:2 and 4:4:4 are the on-site recorder's business, not this encoder's, so
  // the reader refuses them rather than guessing (I444 = 10, P216 = 22).
  REQUIRE(obs_video_format_to_gst(10u) == nullptr);
  REQUIRE(obs_video_format_to_gst(22u) == nullptr);

  // Anything unknown is refused, never guessed at.
  REQUIRE(obs_video_format_to_gst(9999u) == nullptr);
}
