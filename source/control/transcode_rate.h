// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The transcode target a hosted start body may carry, derived from what this
// encoder actually sends.
//
// The portal sizes a destination's transcode from the plan and from the bitrate
// inputs on the allocate request, and an explicit destination bitrate wins
// there outright. Neither figure is the rate this pipeline is running at, and
// nothing on the portal side knows it: on the deployed link the body has
// arrived asking for ~30 Mbit/s of transcode from a 7 Mbit/s source over a
// cellular uplink.
//
// The encoder has no WAN measurement to offer either -- the bridge's link stats
// do not travel back to it -- so the budget it can state is the transport
// ceiling it declares to the receiver and sizes its RIST recovery ring from
// (lib.h's rist_bandwidth_ceiling_kbps). That is an upper bound on the media
// the configured chain is meant to carry, and it is deterministic, unlike any
// figure sampled from a link that is not yet carrying frames.
//
// Rule, and its reasons:
//   - a re-encode cannot carry more detail than its input, so the video rate is
//     the natural ceiling for its transcode;
//   - the transcode shares the uplink with RIST overhead and retransmissions,
//     so it may occupy at most 90% of the transport ceiling (the same headroom
//     the portal's own derivation applies);
//   - the floor keeps a picture worth publishing, unless the ceiling is below
//     the floor -- there the link wins, because a rate above the link is the
//     failure this exists to stop.
//
// The body correction only ever LOWERS a target the portal stated: a value that
// already fits is the portal's decision (DT-22) and stands. A target the portal
// could not derive at all is filled in.

#pragma once

#include <algorithm>
#include <cstdint>

namespace transcode_rate
{

// The lowest rate worth publishing, matching the floor the destination API
// enforces on an explicit bitrate_kbps.
inline constexpr int k_floor_kbps = 500;

// Share of the transport ceiling one transcode may occupy. The remainder is
// RIST overhead, retransmissions, and any sibling output on the same leg.
inline constexpr int k_transport_headroom_pct = 90;

// The highest transcode target this encoder can justify. Zero when either input
// is unknown, which is the caller's signal to leave the portal's number alone.
inline auto cap_kbps(int video_kbps, int transport_ceiling_kbps) -> int
{
  if (video_kbps <= 0 || transport_ceiling_kbps <= 0) {
    return 0;
  }
  const auto budget =
      static_cast<int>(static_cast<std::int64_t>(transport_ceiling_kbps)
                       * k_transport_headroom_pct / 100);
  const int floor = std::min(k_floor_kbps, budget);
  return std::max(std::min(video_kbps, budget), floor);
}

// The value to write into `transcode.bitrate_kbps`: the portal's target when it
// fits under the cap, otherwise the cap. A target the portal left out or stated
// as non-positive is replaced by the cap.
inline auto body_kbps(int portal_kbps,
                      int video_kbps,
                      int transport_ceiling_kbps) -> int
{
  const int cap = cap_kbps(video_kbps, transport_ceiling_kbps);
  if (cap <= 0) {
    return portal_kbps;
  }
  if (portal_kbps <= 0) {
    return cap;
  }
  return std::min(portal_kbps, cap);
}

}  // namespace transcode_rate
