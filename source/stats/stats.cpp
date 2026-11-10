// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <chrono>
#include <cmath>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

#include "stats.h"

#include "stats/bitrate_scale.h"
#include "ui/ui.h"

auto stats::scale_encoder_bitrate_locked(double quality,
                                         cumulative_stats* stats,
                                         const encode_config& encode_config,
                                         int* new_bitrate_out) -> bool
{
  // The algorithm lives in bitrate_scale.cpp, NOT here, so the bonding rig's
  // ABR test and production cannot drift apart. One definition.
  return bitrate_scale::scale_locked(
      quality, stats, encode_config, new_bitrate_out);
}

auto stats::scale_encoder_bitrate(double quality,
                                  cumulative_stats* stats,
                                  const encode_config& encode_config,
                                  int* new_bitrate_out) -> bool
{
  if (stats == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> guard(stats->mutex);
  return scale_encoder_bitrate_locked(
      quality, stats, encode_config, new_bitrate_out);
}

auto stats::got_rist_statistics(const rist_stats& statistics,
                                cumulative_stats* stats,
                                const encode_config& encode_config,
                                user_interface& ui,
                                int* new_bitrate_out) -> bool
{
  if (stats == nullptr) {
    return false;
  }

  bool returnVal = false;

  // Snapshot for UI display so we don't hold the stats lock across FLTK calls.
  int bandwidth_snapshot = 0;
  int bandwidth_avg_snapshot = 0;
  int encode_bitrate_avg_snapshot = 0;
  int retransmitted_sum_snapshot = 0;
  int total_packets_sum_snapshot = 0;
  peer_stats::aggregate peer_totals;
  std::vector<std::string> peer_rows;

  // ONE lock for the whole sample. Locking per step (as before) let the OOB
  // callback on the other thread rewrite current_bitrate between the bitrate
  // decision and the caller's read of it.
  {
    std::lock_guard<std::mutex> guard(stats->mutex);

    if (encode_config.scaling_source.load(std::memory_order_relaxed)
        == bitrate_source::local)
    {
      // Unchanged on purpose: the ABR answers to librist's own per-sample
      // quality and its window mean. The per-peer table below is a DISPLAY
      // rule and must never reach this call (4c).
      returnVal =
          scale_encoder_bitrate_locked(statistics.stats.sender_peer.quality,
                                       stats,
                                       encode_config,
                                       new_bitrate_out);
    }

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

    // 4c: keep the sample per peer identity. librist calls this once per peer
    // per interval, so without the registry the panes render whichever leg
    // reported last and alternate every second on a two-leg bond.
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
    peer_stats::sample sample;
    sample.peer_id = statistics.stats.sender_peer.peer_id;
    sample.cname = statistics.stats.sender_peer.cname;
    sample.quality = statistics.stats.sender_peer.quality;
    sample.rtt_ms = statistics.stats.sender_peer.rtt;
    sample.bandwidth = statistics.stats.sender_peer.bandwidth;
    sample.sent = statistics.stats.sender_peer.sent;
    sample.retransmitted = statistics.stats.sender_peer.retransmitted;
    stats->peers.observe(sample, now_ms);
    stats->peers.prune(now_ms);
    peer_totals = stats->peers.total();
    peer_rows.clear();
    peer_rows.reserve(stats->peers.size());
    for (const peer_stats::entry& peer : stats->peers.peers()) {
      peer_rows.push_back(peer_stats::format_row(peer));
    }

    // The "current" panes show the whole bond: summed counters and the worst
    // leg's quality/RTT, so a good leg cannot mask a bad one and the numbers do
    // not flip between peers.
    bandwidth_snapshot = static_cast<int>(peer_totals.bandwidth);
    bandwidth_avg_snapshot = stats->bandwidth_avg;
    encode_bitrate_avg_snapshot = stats->encode_bitrate_avg;
    retransmitted_sum_snapshot = stats->retransmitted_packets_sum;
    total_packets_sum_snapshot = stats->total_packets_sum;
  }

  ui.lock();
  ui.bandwidth_output->value(std::to_string(bandwidth_snapshot / 1000).c_str());
  ui.link_quality_output->value(
      std::to_string(static_cast<int>(peer_totals.quality)).c_str());
  ui.total_packets_output->value(
      std::to_string(peer_totals.sent).c_str());
  ui.retransmitted_packets_output->value(
      std::to_string(peer_totals.retransmitted).c_str());
  ui.rtt_output->value(std::to_string(peer_totals.rtt_ms).c_str());
  ui.encode_bitrate_output->value(
      std::to_string(encode_config.bitrate.load(std::memory_order_relaxed))
          .c_str());
  ui.set_link_peers(peer_rows);

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
