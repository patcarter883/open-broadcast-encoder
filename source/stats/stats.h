#pragma once

#include "lib/lib.h"
#include "RISTNet.h"
#include <cstdint>

class user_interface;

class stats
{
public:
    static bool got_rist_statistics(const rist_stats& statistics, cumulative_stats *stats, const encode_config &encode_config, user_interface &ui);
};