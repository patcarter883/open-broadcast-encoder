// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <array>
#include <format>
#include <string_view>

#include "encode/video_encoder.h"

namespace
{
// Video-encoder fragment templates indexed by [encoder][codec].
//
// Row order follows enum class encoder: amd, qsv, nvenc, software.
// Column order follows enum class codec: h264, h265, av1.
//
// The single "{}" is the bitrate.
//
// AV1 profile: no av1 encoder element in GStreamer exposes a profile property,
// so the profile is pinned in the caps after av1parse -- the pad that publishes
// the profile, chroma format and bit depth it read from the sequence header.
// The pin allows 8-bit and 10-bit 4:2:0 (both AV1 main) and nothing else, so
// the P010 tier keeps working and a stream the receiver's `vaav1dec` would
// refuse fails caps negotiation here, at Start, instead of on the far end.
constexpr int kEncoderCount = 4;
constexpr int kCodecCount = 3;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
constexpr std::string_view kEncoderTemplates[kEncoderCount][kCodecCount] = {
// encoder::amd
//
// AMF (amfh264enc / amfh265enc / amfav1enc) is Windows-only. Everywhere
// else the same AMD hardware is driven through VAAPI instead
// (vah264enc / vah265enc / vaav1enc), which is also the encoder the
// receiver's transcode tier uses.
#ifdef _WIN32
    {
        // codec::h264
        "amfh264enc name=videncoder  bitrate={} rate-control=cbr "
        "usage=low-latency preset=quality pre-encode=true pa-hqmb-mode=auto ! "
        "video/x-h264,profile=high ! h264parse "
        "config-interval=1 ",
        // codec::h265
        "amfh265enc name=videncoder bitrate={} rate-control=cbr "
        "usage=low-latency preset=quality pre-encode=true pa-hqmb-mode=auto ! "
        "h265parse config-interval=1 ",
        // codec::av1
        "amfav1enc name=videncoder bitrate={} rate-control=cbr "
        "usage=low-latency preset=high-quality  pre-encode=true "
        "pa-hqmb-mode=auto "
        "! av1parse name=av1parser ! video/x-av1,stream-format=obu-stream,"
        "alignment=frame,profile=main ",
    },
#else
    {
        // codec::h264
        "vah264enc name=videncoder bitrate={} rate-control=cbr "
        "key-int-max=60 ! video/x-h264,profile=high ! h264parse "
        "config-interval=1 ",
        // codec::h265
        "vah265enc name=videncoder bitrate={} rate-control=cbr "
        "key-int-max=60 ! h265parse config-interval=1 ",
        // codec::av1
        "vaav1enc name=videncoder bitrate={} rate-control=cbr "
        "key-int-max=60 ! av1parse name=av1parser ! video/x-av1,"
        "stream-format=obu-stream,alignment=frame,profile=main ",
    },
#endif
    // encoder::qsv
    {
        // codec::h264
        "qsvh264enc name=videncoder  bitrate={} rate-control=cbr "
        "target-usage=1 ! h264parse "
        "config-interval=1 ",
        // codec::h265
        "qsvh265enc name=videncoder bitrate={} rate-control=cbr "
        "target-usage=1 ! h265parse "
        "config-interval=1 ",
        // codec::av1
        "qsvav1enc name=videncoder bitrate={} rate-control=cbr "
        "target-usage=1 gop-size=120 ! av1parse name=av1parser ! "
        "video/x-av1,stream-format=obu-stream,alignment=frame,profile=main ",
    },
    // encoder::nvenc
    {
        // codec::h264
        "nvh264enc name=videncoder bitrate={} rc-mode=cbr-hq "
        "preset=low-latency-hq ! h264parse config-interval=1 ",
        // codec::h265
        "nvh265enc name=videncoder bitrate={} rc-mode=cbr-hq "
        "preset=low-latency-hq ! h265parse config-interval=1 ",
        // codec::av1
        "nvav1enc name=videncoder bitrate={} rc-mode=cbr preset=low-latency-hq "
        "! av1parse name=av1parser ! video/x-av1,stream-format=obu-stream,"
        "alignment=frame,profile=main ",
    },
    // encoder::software
    {
        // codec::h264
        "x264enc name=videncoder bitrate={} "
        "speed-preset=fast tune=zerolatency ! h264parse config-interval=1 ",
        // codec::h265
        "x265enc name=videncoder bitrate={} "
        "speed-preset=fast tune=zerolatency ! h265parse config-interval=1 ",
        // codec::av1
        "rav1enc name=videncoder bitrate={} speed-preset=8 tile-cols=2 "
        "tile-rows=2 ! av1parse name=av1parser ! video/x-av1,"
        "stream-format=obu-stream,alignment=frame,profile=main ",
    },
};

auto encoder_row(encoder enc) -> int
{
  switch (enc) {
    case encoder::amd:
      return 0;
    case encoder::qsv:
      return 1;
    case encoder::nvenc:
      return 2;
    case encoder::software:
      return 3;
  }
  return 3;
}

auto codec_column(codec c) -> int
{
  switch (c) {
    case codec::h265:
      return 1;
    case codec::av1:
      return 2;
    case codec::h264:
      return 0;
  }
  return 0;
}

auto field_or_unset(std::string_view value) -> std::string_view
{
  return value.empty() ? std::string_view {"unset"} : value;
}
}  // namespace

auto av1_mezzanine_caps() -> std::string_view
{
  return kAv1MezzanineCaps;
}

auto video_encoder_fragment(encoder enc, codec c, int bitrate_kbps)
    -> std::string
{
  const std::string_view tmpl =
      kEncoderTemplates[encoder_row(enc)][codec_column(c)];
  return std::vformat(tmpl, std::make_format_args(bitrate_kbps));
}

auto av1_bit_depth_of_format(std::string_view format) -> std::string_view
{
  if (format == "NV12" || format == "I420" || format == "YV12") {
    return "8";
  }
  if (format == "P010_10LE" || format == "P010_10BE") {
    return "10";
  }
  if (format == "P012_LE" || format == "P012_BE") {
    return "12";
  }
  return {};
}

auto av1_mezzanine_line(const av1_mezzanine_facts& facts) -> std::string
{
  // The parser's own bit depth is the stream's; the encoder's input format is
  // the same depth by construction (one AV1 tier per input format) and is the
  // one a live pipeline's caps carry from the first buffer.
  const std::string_view bit_depth = facts.parser_bit_depth.empty()
      ? av1_bit_depth_of_format(facts.encoder_input_format)
      : facts.parser_bit_depth;
  return std::format(
      "AV1 mezzanine: profile={} chroma-format={} bit-depth={} "
      "bit tier={}",
      field_or_unset(facts.profile),
      field_or_unset(facts.chroma_format),
      field_or_unset(bit_depth),
      field_or_unset(facts.tier));
}
