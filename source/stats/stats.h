#pragma once

#include <cstdint>

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
};
