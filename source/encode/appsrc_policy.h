// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <cstdint>
#include <format>
#include <string>

// How the two raw-ingest appsrcs are sized and whether a full queue blocks.
//
// The encode path and the preview path read the same OBS wire but must behave
// in opposite ways when their consumer falls behind:
//
//   encode  -- block. A frame the encoder has not taken yet is not a frame it
//   may
//              throw away, and OBS is the right place for that back-pressure.
//   preview -- never block, drop oldest. A dropped preview frame costs a
//   visible
//              stutter; a blocked push stops the reader draining the socket,
//              and OBS drops the whole stream after 90 frames ("peer
//              unreachable").
//
// The values live here, not in the pipeline strings, so the two policies cannot
// drift apart or be changed by accident in one place only.

namespace appsrc_policy
{

// One 1080p BGRA frame, the wire format the OBS plugin sends uncompressed.
inline constexpr std::uint64_t k_frame_1080p_bgra = 1920ull * 1080ull * 4ull;

// ---- Encode: blocking, and deep enough that a scheduling hiccup is not a
// dropped frame. leaky-type=none is stated rather than assumed: a lossy encode
// queue would silently corrupt the stream instead of slowing OBS.
inline constexpr std::uint64_t k_encode_video_max_bytes =
    16ull * 1024ull * 1024ull;
inline constexpr std::uint64_t k_encode_audio_max_bytes =
    1ull * 1024ull * 1024ull;

// ---- Preview: a handful of frames -- enough to ride out one scheduling gap,
// small enough that a stalled sink costs at most that much latency -- and
// bounded by buffer COUNT so the ceiling is frames rather than bytes.
inline constexpr int k_preview_video_max_buffers = 8;
inline constexpr std::uint64_t k_preview_video_max_bytes =
    k_preview_video_max_buffers * k_frame_1080p_bgra;
inline constexpr int k_preview_audio_max_buffers = 64;
inline constexpr std::uint64_t k_preview_audio_max_bytes =
    1ull * 1024ull * 1024ull;

inline std::string encode_video_src()
{
  return std::format(
      "appsrc name=rawvideo is-live=true format=time do-timestamp=false "
      "max-bytes={} block=true leaky-type=none",
      k_encode_video_max_bytes);
}

inline std::string encode_audio_src()
{
  return std::format(
      "appsrc name=rawaudio is-live=true format=time do-timestamp=false "
      "max-bytes={} block=true leaky-type=none",
      k_encode_audio_max_bytes);
}

inline std::string preview_video_src()
{
  return std::format(
      "appsrc name=rawvideo is-live=true format=time do-timestamp=false "
      "max-buffers={} max-bytes={} block=false leaky-type=downstream",
      k_preview_video_max_buffers,
      k_preview_video_max_bytes);
}

inline std::string preview_audio_src()
{
  return std::format(
      "appsrc name=rawaudio is-live=true format=time do-timestamp=false "
      "max-buffers={} max-bytes={} block=false leaky-type=downstream",
      k_preview_audio_max_buffers,
      k_preview_audio_max_bytes);
}

}  // namespace appsrc_policy
