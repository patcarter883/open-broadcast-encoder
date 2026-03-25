#include "statistics_aggregator.h"
#include <algorithm>
#include <numeric>

statistics_aggregator::statistics_aggregator()
  : m_last_prune(std::chrono::steady_clock::now())
{
  m_aggregated.current_bitrate = 4000;  // Default starting bitrate
}

statistics_aggregator::~statistics_aggregator()
{
}

void statistics_aggregator::update_stream_stats(const stream_stats& stats)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  // Find existing or create new
  auto it = std::find_if(m_streams.begin(), m_streams.end(),
      [&stats](const std::shared_ptr<stream_stats>& s) {
        return s->stream_id == stats.stream_id;
      });

  std::shared_ptr<stream_stats> stream_ptr;
  if (it != m_streams.end()) {
    **it = stats;
    stream_ptr = *it;
  } else {
    stream_ptr = std::make_shared<stream_stats>(stats);
    m_streams.push_back(stream_ptr);
  }

  // Prune stale streams periodically
  auto now = std::chrono::steady_clock::now();
  if (now - m_last_prune > PRUNE_INTERVAL) {
    prune_stale_streams();
    m_last_prune = now;
  }

  // Notify individual stream update
  if (m_stream_callback) {
    m_stream_callback(stats);
  }

  notify_aggregated();
}

void statistics_aggregator::remove_stream(const std::string& stream_id)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  m_streams.erase(
      std::remove_if(m_streams.begin(), m_streams.end(),
          [&stream_id](const std::shared_ptr<stream_stats>& s) {
            return s->stream_id == stream_id;
          }),
      m_streams.end());

  notify_aggregated();
}

cumulative_stats statistics_aggregator::get_aggregated_stats() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_aggregated;
}

std::shared_ptr<stream_stats> statistics_aggregator::get_stream_stats(const std::string& stream_id) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = std::find_if(m_streams.begin(), m_streams.end(),
      [&stream_id](const std::shared_ptr<stream_stats>& s) {
        return s->stream_id == stream_id;
      });

  if (it != m_streams.end()) {
    return *it;
  }
  return nullptr;
}

std::vector<stream_stats> statistics_aggregator::get_all_stream_stats() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<stream_stats> result;
  result.reserve(m_streams.size());

  for (const auto& s : m_streams) {
    result.push_back(*s);
  }
  return result;
}

int statistics_aggregator::calculate_consensus_bitrate(int max_bitrate, int min_bitrate) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_streams.empty()) {
    return m_aggregated.current_bitrate;
  }

  // Calculate weighted average based on quality
  // Lower quality streams get more weight (need more protection)
  double total_weight = 0.0;
  int64_t weighted_bitrate = 0;

  for (const auto& stream : m_streams) {
    if (!stream->connected) {
      continue;
    }

    // Weight is inverse of quality (100 - quality)
    // Streams with lower quality need more bitrate adjustment
    double weight = (100.0 - stream->quality) / 100.0;
    if (weight < 0.1) {
      weight = 0.1;  // Minimum weight
    }

    // Use RTT as additional factor - high RTT means less reactive
    double rtt_factor = 1.0;
    if (stream->rtt > 200) {
      rtt_factor = 0.8;  // Reduce weight for high latency streams
    } else if (stream->rtt < 50) {
      rtt_factor = 1.2;  // Boost weight for low latency streams
    }

    weight *= rtt_factor;
    total_weight += weight;

    // Target bitrate based on current bandwidth and quality
    int target_bitrate = static_cast<int>(stream->bandwidth * 0.9);  // 90% of bandwidth
    weighted_bitrate += static_cast<int64_t>(target_bitrate * weight);
  }

  if (total_weight > 0) {
    int consensus = static_cast<int>(weighted_bitrate / total_weight);
    consensus = std::max(std::min(consensus, max_bitrate), min_bitrate);
    return consensus;
  }

  return m_aggregated.current_bitrate;
}

bool statistics_aggregator::should_adjust_bitrate(const encode_config& config) const
{
  int max_bitrate = std::stoi(config.bitrate);
  int target_bitrate = calculate_consensus_bitrate(max_bitrate);

  int diff = std::abs(target_bitrate - m_aggregated.current_bitrate);
  int threshold = max_bitrate * 0.05;  // 5% threshold

  return diff > threshold;
}

void statistics_aggregator::set_stats_callback(stats_callback callback)
{
  m_stats_callback = std::move(callback);
}

void statistics_aggregator::set_stream_callback(stream_stats_callback callback)
{
  m_stream_callback = std::move(callback);
}

size_t statistics_aggregator::active_stream_count() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return std::count_if(m_streams.begin(), m_streams.end(),
      [](const std::shared_ptr<stream_stats>& s) {
        return s->connected;
      });
}

double statistics_aggregator::get_average_quality() const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_streams.empty()) {
    return 100.0;
  }

  double sum = 0.0;
  int count = 0;

  for (const auto& stream : m_streams) {
    if (stream->connected) {
      sum += stream->quality;
      ++count;
    }
  }

  return count > 0 ? sum / count : 100.0;
}

void statistics_aggregator::reset()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_streams.clear();
  m_aggregated = cumulative_stats();
  m_aggregated.current_bitrate = 4000;
}

void statistics_aggregator::notify_aggregated()
{
  if (m_streams.empty()) {
    return;
  }

  // Calculate running averages
  int64_t total_bandwidth = 0;
  int64_t total_retrans = 0;
  int64_t total_packets = 0;
  int connected_count = 0;

  for (const auto& stream : m_streams) {
    if (stream->connected) {
      total_bandwidth += stream->bandwidth;
      total_retrans += stream->retransmitted_packets;
      total_packets += stream->total_packets;
      ++connected_count;
    }
  }

  if (connected_count > 0) {
    m_aggregated.bandwidth_avg = static_cast<int>(total_bandwidth / connected_count);
    m_aggregated.retransmitted_packets_sum = total_retrans;
    m_aggregated.total_packets_sum = total_packets;
  }

  if (m_stats_callback) {
    m_stats_callback(m_aggregated);
  }
}

void statistics_aggregator::prune_stale_streams()
{
  auto now = std::chrono::steady_clock::now();

  m_streams.erase(
      std::remove_if(m_streams.begin(), m_streams.end(),
          [&now](const std::shared_ptr<stream_stats>& s) {
            return (now - s->last_update) > STALE_THRESHOLD;
          }),
      m_streams.end());
}
