// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <optional>
#include <string>

#include "lib/rist_bandwidth.h"

auto rist_bandwidth_kbps(int payload_kbps,
                         std::optional<int> measured_upload_kbps,
                         bool bridged,
                         int bridged_interface_mbps) -> int
{
  if (bridged) {
    return rist_bridged_cap_kbps(bridged_interface_mbps);
  }
  const int stream_kbps = rist_stream_rate_kbps(payload_kbps);
  const int wanted = stream_kbps + rist_direct_headroom_kbps(stream_kbps);
  const bool measured =
      measured_upload_kbps.has_value() && *measured_upload_kbps > 0;
  const int ceiling =
      measured ? std::min(*measured_upload_kbps, wanted) : wanted;
  return std::max(ceiling, stream_kbps);
}

auto rist_direct_headroom_kbps(int stream_kbps) -> int
{
  return stream_kbps * rist_direct_headroom_pct / 100;
}

auto rist_bridged_cap_kbps(int interface_mbps) -> int
{
  if (interface_mbps <= 0) {
    return rist_bridged_ceiling_kbps;
  }
  const int kbps = interface_mbps * 1000;
  return std::max(kbps * rist_bridged_interface_pct / 100,
                  rist_bridged_ceiling_kbps);
}

auto rist_leg_cap_kbps(int session_cap_kbps, int weight, int total_weight)
    -> int
{
  // Weight 0 is SMPTE 2022-7 duplication: the leg carries every packet.
  if (weight <= 0 || total_weight <= 0) {
    return session_cap_kbps;
  }
  return session_cap_kbps * weight / total_weight;
}

auto rist_stream_rate_kbps(int video_kbps) -> int
{
  return (video_kbps + rist_audio_kbps) * (100 + rist_transport_overhead_pct)
         / 100;
}

auto measured_upload_kbps_for(int measured_kbps,
                              const std::string& measured_destination,
                              const std::string& destination)
    -> std::optional<int>
{
  if (measured_kbps <= 0 || measured_destination.empty()
      || measured_destination != destination)
  {
    return std::nullopt;
  }
  return measured_kbps;
}
