// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>

#include "lib/rist_bandwidth.h"

auto rist_bandwidth_kbps(int payload_kbps,
                         std::optional<int> measured_upload_kbps,
                         bool bridged) -> int
{
  if (bridged) {
    return rist_bridged_ceiling_kbps;
  }
  const int wanted = payload_kbps + rist_direct_headroom_kbps;
  const bool measured =
      measured_upload_kbps.has_value() && *measured_upload_kbps > 0;
  const int ceiling =
      measured ? std::min(*measured_upload_kbps, wanted) : wanted;
  return std::max(ceiling, payload_kbps);
}
