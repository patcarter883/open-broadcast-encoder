// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

// How the two raw-ingest appsrcs are sized and whether a full queue blocks.
//
// The encode path and the preview path read the same OBS wire but must behave
// in opposite ways when their consumer falls behind:
//
//   encode  -- block. A frame the encoder has not taken yet is not a frame it
//              may throw away, and OBS is the right place for that
//              back-pressure.
//   preview -- never block, drop oldest. A dropped preview frame costs a
//              visible stutter; a blocked push stops the reader draining the
//              socket, and OBS drops the whole stream after 90 frames ("peer
//              unreachable").
//
// The values live here, not in the pipeline strings, so the two policies cannot
// drift apart or be changed by accident in one place only.

namespace appsrc_policy
{

// One 1080p BGRA frame, the wire format the OBS plugin sends uncompressed. A
// reference size for the sanity checks below, not a sizing input.
inline constexpr std::uint64_t k_frame_1080p_bgra = 1920ull * 1080ull * 4ull;

// ---- Encode: blocking, and deep enough that a scheduling hiccup is not a
// dropped frame. leaky-type=none is stated rather than assumed: a lossy encode
// queue would silently corrupt the stream instead of slowing OBS.
inline constexpr std::uint64_t k_encode_video_max_bytes =
    16ull * 1024ull * 1024ull;
inline constexpr std::uint64_t k_encode_audio_max_bytes =
    1ull * 1024ull * 1024ull;

// ---- Preview: a handful of frames -- enough to ride out one scheduling gap,
// small enough that a stalled sink costs at most that much latency and memory.
// Six frames is 200 ms at 30 fps and 100 ms at 60 fps, longer than a preview
// sink's pipeline turnaround, and 50 MiB at 1080p BGRA.
//
// The depth is a COUNT of the negotiated frames, so the byte cap it implies has
// to follow the caps: the 16 MiB this element used to carry is about two frames
// at 1920x1080, about seven at 960x540 and under one at 3840x2160 -- a depth
// that changes with resolution whether or not the constant does.
// raw_local_input sets the byte cap from the negotiated caps; the value below
// is only what the element carries between construction and the first stream
// header.
inline constexpr int k_preview_video_frames = 6;

// Floor: the fixed cap the preview element carried before it followed the caps,
// about two 1080p BGRA frames. It keeps a small source from ending up with less
// slack than the preview already had.
inline constexpr std::uint64_t k_preview_video_floor_bytes =
    16ull * 1024ull * 1024ull;

// Ceiling: a 4K BGRA frame is 33 MB, and six of them is a couple of hundred
// megabytes held for a preview. Dropping preview frames is always preferable to
// reserving that, so the cap is stated rather than derived.
inline constexpr std::uint64_t k_preview_video_ceiling_bytes =
    128ull * 1024ull * 1024ull;

inline constexpr int k_preview_audio_max_buffers = 64;
inline constexpr std::uint64_t k_preview_audio_max_bytes =
    1ull * 1024ull * 1024ull;

// Bytes one frame of a negotiated raw video format occupies, in eighths of a
// byte per pixel so the sub-byte formats stay integer maths: 4:2:0 is 12/8 of a
// byte per pixel, P010 is 24/8, packed 4:2:2 is 16/8.
//
// An unknown name is treated as the largest case (packed 4-channel). The reader
// refuses a format it cannot map to caps before this is called, so the fallback
// only exists to keep the arithmetic total.
inline auto preview_frame_bytes(const char* gst_format,
                                std::uint32_t width,
                                std::uint32_t height) -> std::uint64_t
{
  const std::string_view format {gst_format != nullptr ? gst_format : ""};
  std::uint64_t eighths_per_pixel = 32;
  if (format == "I420" || format == "NV12") {
    eighths_per_pixel = 12;
  } else if (format == "P010_10LE") {
    eighths_per_pixel = 24;
  } else if (format == "YUY2" || format == "UYVY" || format == "YVYU") {
    eighths_per_pixel = 16;
  } else if (format == "GRAY8") {
    eighths_per_pixel = 8;
  }
  return static_cast<std::uint64_t>(width) * height * eighths_per_pixel / 8ull;
}

// max-bytes for the preview video queue: k_preview_video_frames of the frame
// the caps just negotiated, between the floor and the ceiling above.
inline auto preview_video_max_bytes(const char* gst_format,
                                    std::uint32_t width,
                                    std::uint32_t height) -> std::uint64_t
{
  const std::uint64_t wanted =
      k_preview_video_frames * preview_frame_bytes(gst_format, width, height);
  return std::clamp(
      wanted, k_preview_video_floor_bytes, k_preview_video_ceiling_bytes);
}

// Whether the preview pushes this frame or drops it: `level_bytes` is the
// queue's current fill, `frame_bytes` the frame about to be pushed, `max_bytes`
// the element's cap. A frame is dropped only when the queue already holds
// something, so a cap smaller than one frame can never wedge the preview at
// zero progress -- the first frame always goes in and the sink is then free to
// consume it.
//
// The reader asks BEFORE pushing: block=false would keep the socket alive but a
// full queue then drops inside the element, silently and without a count.
inline auto preview_accepts(std::uint64_t level_bytes,
                            std::uint64_t frame_bytes,
                            std::uint64_t max_bytes) -> bool
{
  return level_bytes == 0 || level_bytes + frame_bytes <= max_bytes;
}

inline auto encode_video_src() -> std::string
{
  return std::format(
      "appsrc name=rawvideo is-live=true format=time do-timestamp=false "
      "max-bytes={} block=true leaky-type=none",
      k_encode_video_max_bytes);
}

inline auto encode_audio_src() -> std::string
{
  return std::format(
      "appsrc name=rawaudio is-live=true format=time do-timestamp=false "
      "max-bytes={} block=true leaky-type=none",
      k_encode_audio_max_bytes);
}

// The preview video element is built before any caps exist, so it starts at the
// floor; the reader raises max-bytes from the negotiated frame size once the
// stream header arrives. max-buffers is the same frame count, so the queue
// holds at most that many frames however generous the byte cap is.
inline auto preview_video_src() -> std::string
{
  return std::format(
      "appsrc name=rawvideo is-live=true format=time do-timestamp=false "
      "max-buffers={} max-bytes={} block=false leaky-type=downstream",
      k_preview_video_frames,
      k_preview_video_floor_bytes);
}

inline auto preview_audio_src() -> std::string
{
  return std::format(
      "appsrc name=rawaudio is-live=true format=time do-timestamp=false "
      "max-buffers={} max-bytes={} block=false leaky-type=downstream",
      k_preview_audio_max_buffers,
      k_preview_audio_max_bytes);
}

}  // namespace appsrc_policy
