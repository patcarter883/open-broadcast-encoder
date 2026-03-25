#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include "transport_base.h"
#include "buffer_distributor.h"
#include "rist_transport.h"
#include "srt_transport.h"
#include "rtmp_transport.h"

/**
 * @brief Manages multiple transport instances and their lifecycle
 * @details Factory and lifecycle manager for transport objects,
 *         creating appropriate transport types based on protocol
 */
class transport_manager
{
public:
  transport_manager();
  ~transport_manager();

  /**
   * @brief Create a new transport for the given configuration
   * @param config Stream configuration
   * @return Shared pointer to created transport
   */
  std::shared_ptr<transport_base> create_transport(const stream_config& config);

  /**
   * @brief Add an existing transport to the manager
   * @param transport Transport to add
   */
  void add_transport(std::shared_ptr<transport_base> transport);

  /**
   * @brief Remove and destroy a transport by ID
   * @param stream_id ID of transport to remove
   * @return true if removed successfully
   */
  bool remove_transport(const std::string& stream_id);

  /**
   * @brief Get transport by ID
   * @param stream_id ID to search for
   * @return Transport pointer or nullptr
   */
  std::shared_ptr<transport_base> get_transport(const std::string& stream_id);

  /**
   * @brief Get all transports
   * @return Vector of transport pointers
   */
  std::vector<std::shared_ptr<transport_base>> get_transports();

  /**
   * @brief Get the buffer distributor
   * @return Reference to buffer distributor
   */
  buffer_distributor& get_distributor();

  /**
   * @brief Start all transports
   */
  void start_all();

  /**
   * @brief Stop all transports
   */
  void stop_all();

  /**
   * @brief Check if any transport is connected
   * @return true if at least one connected
   */
  bool has_connected_transport() const;

  /**
   * @brief Get count of transports
   * @return Number of managed transports
   */
  size_t transport_count() const;

private:
  buffer_distributor m_distributor;
  std::vector<std::shared_ptr<transport_base>> m_transports;
  mutable std::mutex m_mutex;
  uint32_t m_next_stream_id {1};
};
