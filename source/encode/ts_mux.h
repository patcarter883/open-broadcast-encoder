// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <format>
#include <string>
#include <string_view>

// The MPEG-TS mux as the RIST router receives it.
//
// AV1 has no stream type in the MPEG-TS standard, so mpegtsmux refuses the
// caps unless enable-custom-mappings is TRUE ("AV1 requires enabling custom
// mapping which does not have a public specification yet"). With it, the mux
// writes stream_type 0x06 with a registration descriptor 'AV1G' and the av1C
// record as private descriptor 0x80. That pair is what makes the stream
// identifiable at the far end: tsdemux keys its video/x-av1 pad off 'AV1G',
// and a stream_type 0x06 stream without a registration descriptor is dropped
// ("This stream contains no valid or supported streams").
//
// alignment is TS packets per buffer = bytes per RIST datagram (n*188), so a
// lower value keeps packets under a small MTU.
namespace ts_mux
{
// Caps the AV1 branch must hand the muxer; mpegtsmux's AV1 sink template
// accepts nothing else.
inline constexpr std::string_view av1_frame_caps =
    "video/x-av1,stream-format=obu-stream,alignment=frame";

// Registration descriptor mpegtsmux writes for AV1 and tsdemux reads back.
inline constexpr std::string_view av1_registration = "AV1G";

// Stream type AV1 is carried as (private PES).
inline constexpr int av1_stream_type = 0x06;

inline std::string element(int alignment)
{
  return std::format(
      "mpegtsmux alignment={} enable-custom-mappings=true name=tsmux",
      alignment);
}
}  // namespace ts_mux
