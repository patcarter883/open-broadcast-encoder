// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <string>
#include <string_view>

#include "encode/ts_mux.h"
#include "lib/lib.h"

// The name of the AV1 parser instance in the encode pipeline. The mezzanine
// profile is readable from this element's src pad once caps are negotiated.
inline constexpr auto kAv1ParserName = "av1parser";

// The caps every AV1 mezzanine stream carries: the muxer's AV1 caps with the
// profile pinned to main. Main is 8-bit and 10-bit 4:2:0, so it covers both
// encoder tiers (NV12 and P010), and it is the only profile the receiver's
// hardware decoder (`vaav1dec`, sink caps `profile=main`) accepts.
inline constexpr std::string_view kAv1MezzanineCaps =
    "video/x-av1,stream-format=obu-stream,alignment=frame,profile=main";
// The pin is these caps plus one field: it can never drop a field the muxer
// requires.
static_assert(kAv1MezzanineCaps.starts_with(ts_mux::av1_frame_caps));

auto av1_mezzanine_caps() -> std::string_view;

// The video-encoder fragment for one [encoder][codec] cell: the element with
// its settings, the parser, and the caps the parsed stream must carry.
auto video_encoder_fragment(encoder enc, codec c, int bitrate_kbps)
    -> std::string;

// What the mezzanine line reports: the fields the parser publishes in its caps,
// and the pixel format the encoder was fed.
struct av1_mezzanine_facts
{
  std::string_view profile;  // parser caps: main, high, professional
  std::string_view chroma_format;  // parser caps: 4:2:0, 4:2:2, 4:4:4
  std::string_view tier;  // parser caps: main, high
  std::string_view parser_bit_depth;  // parser caps: 8, 10, 12 -- often absent
  std::string_view
      encoder_input_format;  // encoder sink caps: NV12, P010_10LE...
};

// The bit depth the encoder runs at, from the pixel format it was fed. The
// parser's own bit-depth field is the fallback-free source when it is present,
// but av1parse does not put it in the caps of a live pipeline's first buffers,
// so the input format is what the line is built on.
auto av1_bit_depth_of_format(std::string_view format) -> std::string_view;

// The single line logged once per session for the AV1 mezzanine. A field the
// caps did not carry reads as "unset".
auto av1_mezzanine_line(const av1_mezzanine_facts& facts) -> std::string;
