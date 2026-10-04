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
  // Applies one RIST stats sample: drives the adaptive bitrate when the scaling
  // source is `local`, refreshes the rolling UI figures, and reports the new
  // bitrate through *new_bitrate_out (0 when unchanged). Takes ONE lock across
  // the bitrate decision, the deque update and the snapshot, so the OOB
  // callback (a different thread) cannot change current_bitrate in between.
  static bool got_rist_statistics(const rist_stats& statistics,
                                  cumulative_stats* stats,
                                  const encode_config& encode_config,
                                  user_interface& ui,
                                  int* new_bitrate_out);
  // As before, plus *new_bitrate_out so callers never re-read current_bitrate
  // under a second lock.
  static bool scale_encoder_bitrate(double quality,
                                    cumulative_stats* stats,
                                    const encode_config& encode_config,
                                    int* new_bitrate_out);
  // As scale_encoder_bitrate, but the caller already holds stats->mutex.
  static bool scale_encoder_bitrate_locked(double quality,
                                           cumulative_stats* stats,
                                           const encode_config& encode_config,
                                           int* new_bitrate_out);

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
