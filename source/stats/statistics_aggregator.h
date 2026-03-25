#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include "lib/lib.h"

/**
 * @brief Aggregates statistics from multiple output streams
 * @details Provides consensus-based bitrate calculation using
 *         weighted averages across all active streams
 */
class statistics_aggregator
{
public:
  using stats_callback = std::function<void(const cumulative_stats& aggregated_stats)>;
  using stream_stats_callback = std::function<void(const stream_stats& stream)>;

  statistics_aggregator();
  ~statistics_aggregator();

  /**
   * @brief Update statistics for a specific stream
   * @param stats Statistics update for a stream
   */
  void update_stream_stats(const stream_stats& stats);

  /**
   * @brief Remove a stream from aggregation
   * @param stream_id ID of stream to remove
   */
  void remove_stream(const std::string& stream_id);

  /**
   * @brief Get aggregated statistics
   * @return Current aggregated cumulative stats
   */
  cumulative_stats get_aggregated_stats() const;

  /**
   * @brief Get statistics for a specific stream
   * @param stream_id ID of stream
   * @return Stream stats or nullptr if not found
   */
  std::shared_ptr<stream_stats> get_stream_stats(const std::string& stream_id) const;

  /**
   * @brief Get all stream statistics
   * @return Vector of all stream stats
   */
  std::vector<stream_stats> get_all_stream_stats() const;

  /**
   * @brief Calculate consensus bitrate based on all streams
   * @param max_bitrate Maximum allowed bitrate
   * @param min_bitrate Minimum allowed bitrate
   * @return Recommended bitrate
   */
  int calculate_consensus_bitrate(int max_bitrate, int min_bitrate = 1000) const;

  /**
   * @brief Check if bitrate adjustment is needed
   * @param encode_config Current encode configuration
   * @return true if bitrate should change
   */
  bool should_adjust_bitrate(const encode_config& config) const;

  /**
   * @brief Set callback for aggregated stats updates
   * @param callback Function to call
   */
  void set_stats_callback(stats_callback callback);

  /**
   * @brief Set callback for individual stream updates
   * @param callback Function to call
   */
  void set_stream_callback(stream_stats_callback callback);

  /**
   * @brief Get count of active streams
   * @return Number of streams with recent updates
   */
  size_t active_stream_count() const;

  /**
   * @brief Get average quality across all streams
   * @return Average quality percentage
   */
  double get_average_quality() const;

  /**
   * @brief Reset all statistics
   */
  void reset();

private:
  void notify_aggregated();
  void prune_stale_streams();

  std::vector<std::shared_ptr<stream_stats>> m_streams;
  mutable std::mutex m_mutex;
  mutable cumulative_stats m_aggregated;
  stats_callback m_stats_callback;
  stream_stats_callback m_stream_callback;
  std::chrono::steady_clock::time_point m_last_prune;
  static constexpr auto PRUNE_INTERVAL = std::chrono::seconds(5);
  static constexpr auto STALE_THRESHOLD = std::chrono::seconds(10);
};
