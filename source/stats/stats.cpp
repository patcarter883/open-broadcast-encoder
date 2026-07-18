// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <cmath>
#include <mutex>
#include <numeric>
#include <string>

#include "stats.h"

#include "ui/ui.h"

auto stats::scale_encoder_bitrate(double quality,
                                  cumulative_stats* stats,
                                  const encode_config& encode_config) -> bool
{
  if (stats == nullptr) {
    return false;
  }
  if (encode_config.bitrate.load(std::memory_order_relaxed) <= 0) {
    return false;
  }
  if (std::isnan(quality) || std::isinf(quality)) {
    return false;
  }

  std::lock_guard<std::mutex> guard(stats->mutex);

  int bitrateDelta = 0;
  double qualDiffPct = 0.0;
  int adjBitrate = 0;
  const double maxBitrate =
      static_cast<double>(encode_config.bitrate.load(std::memory_order_relaxed));
  bool returnVal = false;

  if (stats->previous_quality > 0.0
      && static_cast<int>(quality) != static_cast<int>(stats->previous_quality))
  {
    qualDiffPct = quality / stats->previous_quality;
    adjBitrate = static_cast<int>(stats->current_bitrate * qualDiffPct);
    bitrateDelta = adjBitrate - stats->current_bitrate;
  }

  if (static_cast<int>(stats->previous_quality) == 100
      && static_cast<int>(quality) == 100
      && static_cast<double>(stats->current_bitrate) < maxBitrate
      && maxBitrate > 0.0)
  {
    qualDiffPct = static_cast<double>(stats->current_bitrate) / maxBitrate;
    adjBitrate = static_cast<int>(stats->current_bitrate * (1.0 + qualDiffPct));
    bitrateDelta = adjBitrate - stats->current_bitrate;
  }

  if (bitrateDelta != 0
      || maxBitrate < static_cast<double>(stats->current_bitrate))
  {
    int candidate = stats->current_bitrate + bitrateDelta / 2;
    int newBitrate =
        std::max(std::min(candidate, static_cast<int>(maxBitrate)), 1000);
    stats->current_bitrate = newBitrate;
    returnVal = true;
  }

  stats->previous_quality = quality;

  return returnVal;
}

auto stats::got_rist_statistics(const rist_stats& statistics,
                                cumulative_stats* stats,
                                const encode_config& encode_config,
                                user_interface& ui) -> bool
{
  if (stats == nullptr) {
    return false;
  }

  bool returnVal = false;

  if (encode_config.scaling_source.load(std::memory_order_relaxed)
      == bitrate_source::local)
  {
    returnVal = scale_encoder_bitrate(
        statistics.stats.sender_peer.quality, stats, encode_config);
  }

  // Snapshot for UI display so we don't hold the stats lock across FLTK calls.
  int bandwidth_snapshot = 0;
  int bandwidth_avg_snapshot = 0;
  int encode_bitrate_avg_snapshot = 0;
  int retransmitted_sum_snapshot = 0;
  int total_packets_sum_snapshot = 0;

  {
    std::lock_guard<std::mutex> guard(stats->mutex);

    stats::push_bounded(stats->bandwidth,
                        statistics.stats.sender_peer.bandwidth);
    stats::push_bounded(stats->encode_bitrate, stats->current_bitrate);
    stats::push_bounded(stats->retransmitted_packets,
                        statistics.stats.sender_peer.retransmitted);
    stats::push_bounded(stats->total_packets,
                        statistics.stats.sender_peer.sent);

    stats->bandwidth_avg = std::accumulate(stats->bandwidth.begin(),
                                           stats->bandwidth.end(),
                                           0,
                                           [n = 0](auto cma, auto i) mutable
                                           { return cma + (i - cma) / ++n; });
    stats->encode_bitrate_avg = std::accumulate(
        stats->encode_bitrate.begin(),
        stats->encode_bitrate.end(),
        0,
        [n = 0](auto cma, auto i) mutable { return cma + (i - cma) / ++n; });
    stats->retransmitted_packets_sum =
        std::accumulate(stats->retransmitted_packets.begin(),
                        stats->retransmitted_packets.end(),
                        0);
    stats->total_packets_sum = std::accumulate(
        stats->total_packets.begin(), stats->total_packets.end(), 0);

    bandwidth_snapshot = statistics.stats.sender_peer.bandwidth;
    bandwidth_avg_snapshot = stats->bandwidth_avg;
    encode_bitrate_avg_snapshot = stats->encode_bitrate_avg;
    retransmitted_sum_snapshot = stats->retransmitted_packets_sum;
    total_packets_sum_snapshot = stats->total_packets_sum;
  }

  ui.lock();
  ui.bandwidth_output->value(std::to_string(bandwidth_snapshot / 1000).c_str());
  ui.link_quality_output->value(
      std::to_string(statistics.stats.sender_peer.quality).c_str());
  ui.total_packets_output->value(
      std::to_string(statistics.stats.sender_peer.sent).c_str());
  ui.retransmitted_packets_output->value(
      std::to_string(statistics.stats.sender_peer.retransmitted).c_str());
  ui.rtt_output->value(
      std::to_string(statistics.stats.sender_peer.rtt).c_str());
  ui.encode_bitrate_output->value(
      std::to_string(encode_config.bitrate.load(std::memory_order_relaxed))
          .c_str());

  ui.cumulative_bandwidth_output->value(
      std::to_string(bandwidth_avg_snapshot / 1000).c_str());
  ui.cumulative_encode_bitrate_output->value(
      std::to_string(encode_bitrate_avg_snapshot).c_str());
  ui.cumulative_retransmitted_packets_output->value(
      std::to_string(retransmitted_sum_snapshot).c_str());
  ui.cumulative_total_packets_output->value(
      std::to_string(total_packets_sum_snapshot).c_str());

  ui.unlock();

  return returnVal;
}
