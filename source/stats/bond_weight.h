// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// The weight a bonded leg is given, from that leg's own RIST sender stats.
//
// librist divides the payload rotation between the peers of a session in
// proportion to their weights, so a weight is a statement about how much of the
// upload a leg can carry. There is no download probe in this configuration and
// nothing is measured on the far side: the estimate uses only what the sender
// already knows about the leg it is writing to.
//
//   carried   = the leg's bandwidth from the sender stats (its share of the
//               payload, in kbps)
//   share     = the fraction of the rotation that leg was given when it carried
//               that figure, so carried/share is what it would carry with the
//               whole rotation. This is what keeps the loop from starving a
//               lightly weighted leg: its figure is scaled back up rather than
//               read as its capacity.
//   retx      = the share of its packets that had to be retransmitted
//   rtt       = its round-trip time against the recovery RTT floor: a leg at N
//               times that floor returns 1/N of the credit, because its
//               retransmit window drains N times slower.
//
//   available = carried / share * (1 - retx) * rtt_credit
//
// The mapping is deliberately absolute (kbps per weight unit) rather than
// normalised per bond, so one leg's weight says the same thing whether it is
// bonded with one other leg or five.
namespace bond_weight
{
struct leg_sample
{
  // struct rist_stats_sender_peer::bandwidth, bytes per second.
  std::uint64_t bandwidth_bytes_per_s = 0;
  std::uint32_t rtt_ms = 0;
  // Cumulative counters from the same stats sample.
  std::uint64_t sent = 0;
  std::uint64_t retransmitted = 0;
};

struct estimate
{
  double available_kbps = 0.0;
  std::uint32_t weight = 1;
};

// 0 is not a small weight: librist reads it as "duplicate every packet on every
// leg". The floor therefore has to be 1.
inline constexpr std::uint32_t k_min_weight = 1;
// The ceiling, so one fast leg cannot swamp a bond of slow ones and a stale
// estimate cannot ask for an unbounded share.
inline constexpr std::uint32_t k_max_weight = 100;
// Weight units per kbit/s: a leg judged able to carry 5 Mbit/s gets weight 50.
inline constexpr double k_kbps_per_weight = 100.0;
// RTT at which a leg still gets full credit (the recovery RTT floor this
// encoder configures), in milliseconds.
inline constexpr double k_rtt_full_credit_ms = 100.0;

// `rotation_share` is the leg's fraction of the bond's total weight when the
// sample was taken (0 when nothing is known yet).
inline auto estimate_leg(const leg_sample& sample, double rotation_share)
    -> estimate
{
  const double carried_kbps =
      static_cast<double>(sample.bandwidth_bytes_per_s) * 8.0 / 1000.0;

  const double share = std::clamp(rotation_share, 0.0, 1.0);
  const double capacity_kbps =
      share > 0.0 ? carried_kbps / share : carried_kbps;

  const std::uint64_t attempts = sample.sent + sample.retransmitted;
  const double retransmit_share = attempts == 0
      ? 0.0
      : static_cast<double>(sample.retransmitted)
          / static_cast<double>(attempts);

  const double rtt_credit = k_rtt_full_credit_ms
      / std::max(k_rtt_full_credit_ms, static_cast<double>(sample.rtt_ms));

  estimate result;
  result.available_kbps = capacity_kbps * (1.0 - retransmit_share) * rtt_credit;
  const double units = std::round(result.available_kbps / k_kbps_per_weight);
  result.weight =
      static_cast<std::uint32_t>(std::clamp(units,
                                            static_cast<double>(k_min_weight),
                                            static_cast<double>(k_max_weight)));
  return result;
}
}  // namespace bond_weight
