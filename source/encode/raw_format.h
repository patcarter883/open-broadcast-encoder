// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <cstdint>

// Maps the video format OBS puts on the raw wire to the GStreamer caps format
// name, or nullptr when this reader does not accept that format.
//
// The wire value IS libobs' `enum video_format`, sent verbatim by
// obs-raw-output/src/raw-output.c. The numbers below are that enum's values --
// authoritative source /usr/include/obs/media-io/video-io.h -- so each one is
// named here and used by name in the switch. A bare literal there is how the
// wrong number ends up silently bound to the wrong format.
//
// No GStreamer or libobs headers: this stays a pure function, so it can be
// exercised without a pipeline or a GPU.
constexpr std::uint32_t kObsI420 = 1u;
constexpr std::uint32_t kObsNv12 = 2u;
constexpr std::uint32_t kObsYvyu = 3u;
constexpr std::uint32_t kObsYuy2 = 4u;
constexpr std::uint32_t kObsUyvy = 5u;
constexpr std::uint32_t kObsRgba = 6u;
constexpr std::uint32_t kObsBgra = 7u;
constexpr std::uint32_t kObsBgrx = 8u;
constexpr std::uint32_t kObsY800 = 9u;
constexpr std::uint32_t kObsI444 = 10u;
constexpr std::uint32_t kObsI010 = 17u;
constexpr std::uint32_t kObsP010 = 18u;
constexpr std::uint32_t kObsI210 = 19u;
constexpr std::uint32_t kObsP216 = 22u;
constexpr std::uint32_t kObsP416 = 23u;

auto obs_video_format_to_gst(std::uint32_t format) -> const char*;

// True when the format carries more than 8 bits per sample.
//
// Bit depth is a property of the format itself, not of this reader's acceptance
// of it: the mapping above refuses 4:2:2/4:4:4 (the on-site recorder's
// business), but the codec and destination policy still has to know whether the
// stream it is about to carry is 10-bit.
auto obs_video_format_is_10bit(std::uint32_t format) -> bool;
