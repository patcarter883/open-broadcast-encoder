#pragma once

#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include "lib/lib.h"

/**
 * @brief Abstract base class for all transport implementations
 * @details Provides a common interface for RIST, SRT, and RTMP transports
 *         allowing the buffer distributor to work with any protocol
 */
class transport_base
{
public:
  using shared_buffer_ptr = std::shared_ptr<shared_buffer>;
  using send_callback = std::function<void(bool success, const std::string& error)>;
  using stats_callback = std::function<void(const stream_stats& stats)>;

  transport_base() = default;
  virtual ~transport_base() = default;

  // Non-copyable
  transport_base(const transport_base&) = delete;
  transport_base& operator=(const transport_base&) = delete;

  // Movable
  transport_base(transport_base&&) = default;
  transport_base& operator=(transport_base&&) = default;

  /**
   * @brief Initialize the transport with configuration
   * @param config Stream configuration for this transport
   * @return true if initialization succeeded
   */
  virtual bool initialize(const stream_config& config) = 0;

  /**
   * @brief Start the transport (begin listening/connecting)
   * @return true if started successfully
   */
  virtual bool start() = 0;

  /**
   * @brief Stop the transport
   */
  virtual void stop() = 0;

  /**
   * @brief Send a buffer through the transport
   * @param buffer Shared buffer to send
   * @param callback Optional callback for send completion
   */
  virtual void send_buffer(shared_buffer_ptr buffer, send_callback callback = nullptr) = 0;

  /**
   * @brief Check if transport is currently connected
   * @return true if connected
   */
  virtual bool is_connected() const = 0;

  /**
   * @brief Get the stream ID for this transport
   * @return Stream identifier string
   */
  virtual std::string get_stream_id() const = 0;

  /**
   * @brief Get the protocol type for this transport
   * @return transport_protocol enum value
   */
  virtual transport_protocol get_protocol() const = 0;

  /**
   * @brief Get current statistics for this stream
   * @return stream_stats structure
   */
  virtual stream_stats get_stats() const = 0;

  /**
   * @brief Set statistics update callback
   * @param callback Function to call with updated stats
   */
  void set_stats_callback(stats_callback callback);

  /**
   * @brief Set logging callback
   * @param callback Function to call for log messages
   */
  void set_log_callback(std::function<void(const std::string& msg)> callback);

protected:
  /**
   * @brief Notify stats update to registered callback
   * @param stats Statistics to report
   */
  void notify_stats(const stream_stats& stats);

  /**
   * @brief Log a message via registered callback
   * @param msg Message to log
   */
  void log_message(const std::string& msg);

  stats_callback m_stats_callback;
  std::function<void(const std::string& msg)> m_log_callback;
  stream_config m_config;
  stream_stats m_stats;
};
