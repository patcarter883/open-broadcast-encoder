// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <cstdint>

namespace fps_snap
{

// Snap a measured frame rate to the standard broadcast rate it is running at:
// 24, 25, 30, 50 or 60. Two seconds of frames must land on a whole number, so a
// source measuring 29.97 or 30.17 yields 30 -- a gop of 60, not 59 or 61 -- and
// a stalled source reporting 29.0 yields 30 rather than a gop of 58. Integer
// milli-frames-per-second throughout: no floating point and no rounding
// library.
//
// Header-only and header-private by design: it is pure arithmetic with no
// GStreamer or app state, so the rate rule can be pinned by a test that links
// nothing else.
//
// NOT in a namespace called `encode`: this repo already has a class of that
// name in the global namespace (lib.h, encode.h), and the two collide -- the
// compiler reports the class "redeclared as different kind of entity" and every
// member function detaches. `fps_snap` is deliberately a name nothing else
// uses.
//
// Zero in, zero out: a caller with no measurement must leave a rate-dependent
// value alone rather than guess at one.
inline std::uint32_t standard_rate(std::uint32_t num, std::uint32_t den)
{
  if (num == 0 || den == 0) {
    return 0;
  }
  constexpr std::uint32_t k_standard_milli[5] = {
      24000, 25000, 30000, 50000, 60000};
  const auto milli_fps = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(num) * 1000ULL) / den);
  std::uint32_t best = 0;
  std::uint32_t best_delta = UINT32_MAX;
  for (const std::uint32_t candidate : k_standard_milli) {
    const std::uint32_t delta =
        candidate > milli_fps ? candidate - milli_fps : milli_fps - candidate;
    if (delta < best_delta) {
      best_delta = delta;
      best = candidate;
    }
  }
  return best / 1000;
}

}  // namespace fps_snap
