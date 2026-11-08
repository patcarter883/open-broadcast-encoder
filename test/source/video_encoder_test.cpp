// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The AV1 half of the encoder fragment, asserted as a string because that
// string is the contract: the encoder carries no profile property, so the caps
// after av1parse are what pin the mezzanine to the one profile the receiver's
// hardware decoder advertises (`vaav1dec` sink caps: `profile=main`).
//
// Which bit depth the mezzanine carries is decided by the ingest (NV12 vs
// P010), not here: AV1 main is 8-bit and 10-bit 4:2:0, and both were measured
// on this box (vaav1enc: NV12 -> bit-depth 8, P010_10LE -> bit-depth 10, both
// `profile=main`). The pin therefore carries no bit-depth field.

#include <array>
#include <string>
#include <string_view>

#include "encode/video_encoder.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
std::array<encoder, 4> all_encoders()
{
  return {encoder::amd, encoder::qsv, encoder::nvenc, encoder::software};
}
}  // namespace

TEST_CASE("the AV1 mezzanine caps are profile main", "[video_encoder]")
{
  const std::string_view caps = av1_mezzanine_caps();
  CHECK(caps
        == "video/x-av1,stream-format=obu-stream,alignment=frame,"
           "profile=main");
  // Main covers 8-bit and 10-bit 4:2:0; a bit-depth pin here would refuse the
  // P010 tier.
  CHECK(caps.find("bit-depth") == std::string_view::npos);
  // The pin is the muxer's AV1 caps plus the profile field, so it cannot drift
  // from what mpegtsmux accepts.
  CHECK(caps.starts_with(ts_mux::av1_frame_caps));
}

TEST_CASE("the qsv AV1 fragment pins the mezzanine profile", "[video_encoder]")
{
  const std::string fragment =
      video_encoder_fragment(encoder::qsv, codec::av1, 6000);

  // The encoder element and its settings are unaffected by this change.
  CHECK(fragment.find("qsvav1enc name=videncoder bitrate=6000 "
                      "rate-control=cbr target-usage=1 gop-size=120")
        != std::string::npos);
  // The cap sits downstream of the parser, which is the pad that publishes the
  // profile it read from the sequence header.
  const std::size_t parser = fragment.find("av1parse");
  const std::size_t caps = fragment.find(av1_mezzanine_caps());
  REQUIRE(parser != std::string::npos);
  REQUIRE(caps != std::string::npos);
  CHECK(parser < caps);
  // Named, so the encode pipeline can read the negotiated profile off its src
  // pad and log it.
  CHECK(fragment.find(std::string {"av1parse name="} + kAv1ParserName)
        != std::string::npos);
}

TEST_CASE("every AV1 fragment pins the mezzanine profile", "[video_encoder]")
{
  for (const encoder enc : all_encoders()) {
    INFO("encoder index " << static_cast<int>(enc));
    const std::string fragment = video_encoder_fragment(enc, codec::av1, 6000);
    CHECK(fragment.find(av1_mezzanine_caps()) != std::string::npos);
    CHECK(fragment.find(std::string {"av1parse name="} + kAv1ParserName)
          != std::string::npos);
  }
}

TEST_CASE("the profile pin is AV1-only", "[video_encoder]")
{
  for (const encoder enc : all_encoders()) {
    INFO("encoder index " << static_cast<int>(enc));
    CHECK(video_encoder_fragment(enc, codec::h264, 6000).find("profile=main")
          == std::string::npos);
    CHECK(video_encoder_fragment(enc, codec::h265, 6000).find("profile=main")
          == std::string::npos);
  }
}

TEST_CASE("the logged mezzanine line names profile and bit depth",
          "[video_encoder]")
{
  const auto line = [](std::string_view profile,
                       std::string_view chroma,
                       std::string_view parser_depth,
                       std::string_view tier,
                       std::string_view input_format)
  {
    return av1_mezzanine_line({.profile = profile,
                               .chroma_format = chroma,
                               .tier = tier,
                               .parser_bit_depth = parser_depth,
                               .encoder_input_format = input_format});
  };

  // The two tiers this product carries, as a live pipeline reports them: the
  // parser names the profile, the encoder's input format names the depth.
  CHECK(line("main", "4:2:0", "", "main", "NV12")
        == "AV1 mezzanine: profile=main chroma-format=4:2:0 bit-depth=8 bit "
           "tier=main");
  CHECK(line("main", "4:2:0", "", "main", "P010_10LE")
        == "AV1 mezzanine: profile=main chroma-format=4:2:0 bit-depth=10 bit "
           "tier=main");
  // A parser bit depth, when the caps do carry one, wins over the input format.
  CHECK(line("main", "4:2:0", "10", "main", "NV12")
        == "AV1 mezzanine: profile=main chroma-format=4:2:0 bit-depth=10 bit "
           "tier=main");
  // Caps fields that were not there read as unset rather than blank.
  CHECK(line("", "", "", "", "")
        == "AV1 mezzanine: profile=unset chroma-format=unset bit-depth=unset "
           "bit tier=unset");
}

TEST_CASE("the encoder's input formats map to the AV1 bit depth",
          "[video_encoder]")
{
  CHECK(av1_bit_depth_of_format("NV12") == "8");
  CHECK(av1_bit_depth_of_format("P010_10LE") == "10");
  CHECK(av1_bit_depth_of_format("P012_LE") == "12");
  CHECK(av1_bit_depth_of_format("RGB") == "");
}
