#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include "transport_base.h"
#include "lib/lib.h"

/**
 * @brief Distributes encoded buffers to multiple transport outputs
 * @details Takes a single buffer from the encoder pipeline and distributes
 *         it to all registered transports using shared_ptr for zero-copy
 *         multi-consumer delivery
 */
class buffer_distributor
{
public:
  using transport_ptr = std::shared_ptr<transport_base>;
  using connection_callback = std::function<void(const std::string& stream_id, bool connected)>;

  buffer_distributor();
  ~buffer_distributor();

  /**
   * @brief Add a transport to receive buffer distributions
   * @param transport Shared pointer to transport
   */
  void add_transport(transport_ptr transport);

  /**
   * @brief Remove a transport by stream ID
   * @param stream_id Identifier of stream to remove
   * @return true if transport was found and removed
   */
  bool remove_transport(const std::string& stream_id);

  /**
   * @brief Get a transport by stream ID
   * @param stream_id Identifier of stream
   * @return Transport pointer or nullptr if not found
   */
  transport_ptr get_transport(const std::string& stream_id);

  /**
   * @brief Get all registered transports
   * @return Vector of transport pointers
   */
  std::vector<transport_ptr> get_transports() const;

  /**
   * @brief Distribute a buffer to all registered transports
   * @param buffer Buffer to distribute
   */
  void distribute(transport_base::shared_buffer_ptr buffer);

  /**
   * @brief Convert legacy buffer_data to shared_buffer and distribute
   * @param buf Legacy buffer data
   */
  void distribute_legacy(const buffer_data& buf);

  /**
   * @brief Set callback for connection state changes
   * @param callback Function to call on connection changes
   */
  void set_connection_callback(connection_callback callback);

  /**
   * @brief Check if any transport is connected
   * @return true if at least one transport is connected
   */
  bool has_connected_transport() const;

  /**
   * @brief Get number of registered transports
   * @return Transport count
   */
  size_t transport_count() const;

  /**
   * @brief Start all transports
   */
  void start_all();

  /**
   * @brief Stop all transports
   */
  void stop_all();

private:
  std::vector<transport_ptr> m_transports;
  mutable std::mutex m_mutex;
  connection_callback m_connection_callback;
  std::atomic<bool> m_running {false};
};
