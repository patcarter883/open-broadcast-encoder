#include <numeric>
#include <string>

#include "stats.h"

#include "ui/ui.h"

auto stats::scale_encoder_bitrate(double quality,
                                  cumulative_stats* stats,
                                  const encode_config& encode_config) -> bool
{
  int bitrateDelta = 0;
  double qualDiffPct;
  int adjBitrate;
  double maxBitrate = static_cast<double>(encode_config.bitrate);
  bool returnVal = false;

  if (stats->previous_quality > 0
      && (int)quality != (int)stats->previous_quality)
  {
    qualDiffPct = quality / stats->previous_quality;
    adjBitrate = (int)(stats->current_bitrate * qualDiffPct);
    bitrateDelta = adjBitrate - stats->current_bitrate;
  }

  if (static_cast<int>(stats->previous_quality) == 100
      && static_cast<int>(quality) == 100
      && stats->current_bitrate < maxBitrate)
  {
    qualDiffPct = (stats->current_bitrate / maxBitrate);
    adjBitrate = (int)(stats->current_bitrate * (1 + qualDiffPct));
    bitrateDelta = adjBitrate - stats->current_bitrate;
  }

  if (bitrateDelta != 0 || maxBitrate < stats->current_bitrate) {
    int newBitrate = std::max(
        std::min(stats->current_bitrate += bitrateDelta / 2, (int)maxBitrate),
        static_cast<const int>(1000));
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
  bool returnVal = false;

  if (encode_config.scaling_source == bitrate_source::local) {
    returnVal = scale_encoder_bitrate(
        statistics.stats.sender_peer.quality, stats, encode_config);
  }

  stats->bandwidth.push_back(statistics.stats.sender_peer.bandwidth);
  if (stats->bandwidth.size() > 1000)
    stats->bandwidth.erase(stats->bandwidth.begin());
  stats->encode_bitrate.push_back(stats->current_bitrate);
  if (stats->encode_bitrate.size() > 1000)
    stats->encode_bitrate.erase(stats->encode_bitrate.begin());
  stats->retransmitted_packets.push_back(
      statistics.stats.sender_peer.retransmitted);
  if (stats->retransmitted_packets.size() > 1000)
    stats->retransmitted_packets.erase(stats->retransmitted_packets.begin());
  stats->total_packets.push_back(statistics.stats.sender_peer.sent);
  if (stats->total_packets.size() > 1000)
    stats->total_packets.erase(stats->total_packets.begin());

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

  ui.lock();
  ui.bandwidth_output->value(
      std::to_string(statistics.stats.sender_peer.bandwidth / 1000).c_str());
  ui.link_quality_output->value(
      std::to_string(statistics.stats.sender_peer.quality).c_str());
  ui.total_packets_output->value(
      std::to_string(statistics.stats.sender_peer.sent).c_str());
  ui.retransmitted_packets_output->value(
      std::to_string(statistics.stats.sender_peer.retransmitted).c_str());
  ui.rtt_output->value(
      std::to_string(statistics.stats.sender_peer.rtt).c_str());
  ui.encode_bitrate_output->value(
      std::to_string(encode_config.bitrate).c_str());

  ui.cumulative_bandwidth_output->value(
      std::to_string(stats->bandwidth_avg / 1000).c_str());
  ui.cumulative_encode_bitrate_output->value(
      std::to_string(stats->encode_bitrate_avg).c_str());
  ui.cumulative_retransmitted_packets_output->value(
      std::to_string(stats->retransmitted_packets_sum).c_str());
  ui.cumulative_total_packets_output->value(
      std::to_string(stats->total_packets_sum).c_str());

  ui.unlock();

  return returnVal;
}
