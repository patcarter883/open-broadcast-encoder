// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "encode/raw_format.h"

auto obs_video_format_to_gst(std::uint32_t format) -> const char*
{
  switch (format) {
    case kObsI420:
      return "I420";
    case kObsNv12:
      return "NV12";
    case kObsYvyu:
      return "YVYU";
    case kObsYuy2:
      return "YUY2";
    case kObsUyvy:
      return "UYVY";
    case kObsRgba:
      return "RGBA";
    case kObsBgra:
      return "BGRA";
    // GStreamer spells the padding byte of this packed format lowercase, so the
    // name does not match libobs' VIDEO_FORMAT_BGRX spelling. Not a typo.
    case kObsBgrx:
      return "BGRx";
    case kObsY800:
      return "GRAY8";
    // 10-bit 4:2:0, two planes of 16-bit samples. GStreamer names this format
    // P010_10LE and it maps exactly, so this path needs no repack.
    case kObsP010:
      return "P010_10LE";
    default:
      return nullptr;
  }
}

auto obs_video_format_is_10bit(std::uint32_t format) -> bool
{
  switch (format) {
    // Every 10-bit value in libobs' enum: the 4:2:0 pairs (I010/P010), the
    // 4:2:2 pairs (I210/P216) and 4:4:4 (P416). Only P010 is one this reader
    // accepts today; the rest are still reported as 10-bit so the policy sees
    // the depth rather than silently treating an unsupported format as 8-bit.
    case kObsI010:
    case kObsP010:
    case kObsI210:
    case kObsP216:
    case kObsP416:
      return true;
    default:
      return false;
  }
}
