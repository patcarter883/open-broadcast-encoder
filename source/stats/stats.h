// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include "RISTNet.h"
#include "lib/lib.h"

class user_interface;

class stats
{
public:
  static bool got_rist_statistics(const rist_stats& statistics,
                                  cumulative_stats* stats,
                                  const encode_config& encode_config,
                                  user_interface& ui);
  static bool scale_encoder_bitrate(double quality,
                                    cumulative_stats* stats,
                                    const encode_config& encode_config);

  static inline void push_bounded(std::deque<int>& d,
                                  int value,
                                  std::size_t cap = 1000)
  {
    d.push_back(value);
    if (d.size() > cap) {
      d.pop_front();
    }
  }
};
