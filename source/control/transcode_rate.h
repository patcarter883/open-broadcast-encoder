// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The transcode target a hosted start body may carry, derived from what this
// encoder actually sends and from what the link carries.
//
// The portal sizes a destination's transcode from the plan and from the bitrate
// inputs on the allocate request, and an explicit destination bitrate wins
// there outright. Neither figure is the rate this pipeline is running at, and
// nothing on the portal side knows it: on the deployed link the body has
// arrived asking for ~30 Mbit/s of transcode from a 7 Mbit/s source over a
// cellular uplink.
//
// The rates that bound a target live on this side of the chain:
//   video_kbps        what this encoder is configured to send. The transcode
//                     re-encodes that source, so a target above it asks for
//                     detail the source never carried.
//   link_budget_kbps  the aggregate rate the bridge measured on its WAN legs at
//                     the last successful calibration. Zero when no link has
//                     been measured since this launch.
//   ceiling_kbps      the bandwidth the RIST leg declares and sizes its
//                     recovery ring from (lib.h's rist_bandwidth_ceiling_kbps).
//                     libRIST spends it on retransmissions as well as the
//                     payload, so it is never fully available to a target.
//
// Rule, and its reasons:
//   - the target is the smallest rate that is known, so a measurement of a
//     trickle outranks a plan-sized number and a measurement of plenty cannot
//     exceed what is being sent;
//   - with no measurement the sending rate and the ceiling still bound it,
//     which makes the result deterministic rather than link-dependent;
//   - the ceiling contributes at most 90%, the same headroom the portal's own
//     derivation applies, because the remainder is retransmission budget on the
//     same leg;
//   - the floor keeps a picture worth publishing, unless every known bound is
//     below the floor -- there the link wins, because a rate above the link is
//     the failure this exists to stop.
//
// Zero means nothing is known and the caller leaves the portal's figure alone.
// The correction only ever LOWERS a target the portal stated: a value that
// already fits is the portal's decision (DT-22) and stands. A target the portal
// could not derive at all is filled in.

#pragma once

#include <algorithm>

namespace transcode_rate
{

// The lowest rate worth publishing, matching the floor the destination API
// enforces on an explicit bitrate_kbps.
inline constexpr int k_floor_kbps = 500;

// Share of the transport ceiling one transcode may occupy. The remainder is
// RIST overhead and retransmissions on the same leg.
inline constexpr int k_ceiling_headroom_pct = 90;

// The rate every transcode target in the body is bounded by. Zero when nothing
// is known, which is the caller's signal to leave the portal's numbers alone.
inline int resolve(int video_kbps, int link_budget_kbps, int ceiling_kbps)
{
  int upper = 0;
  const auto tighten = [&upper](int rate)
  {
    if (rate > 0 && (upper == 0 || rate < upper))
    {
      upper = rate;
    }
  };
  tighten(video_kbps);
  if (ceiling_kbps > 0)
  {
    tighten(ceiling_kbps * k_ceiling_headroom_pct / 100);
  }
  if (upper == 0)
  {
    return 0;
  }
  const int target =
      link_budget_kbps > 0 ? std::min(link_budget_kbps, upper) : upper;
  return target < k_floor_kbps && upper >= k_floor_kbps ? k_floor_kbps : target;
}

// The value to write into `transcode.bitrate_kbps`: the portal's target when it
// fits under the resolved rate, otherwise that rate. A target the portal left
// out or stated as non-positive is replaced by the resolved rate.
inline int body_kbps(int portal_kbps,
                     int video_kbps,
                     int link_budget_kbps,
                     int ceiling_kbps)
{
  const int target = resolve(video_kbps, link_budget_kbps, ceiling_kbps);
  if (target <= 0)
  {
    return portal_kbps;
  }
  if (portal_kbps <= 0)
  {
    return target;
  }
  return std::min(portal_kbps, target);
}

}  // namespace transcode_rate
