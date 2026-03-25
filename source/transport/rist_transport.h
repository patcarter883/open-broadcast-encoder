#pragma once

#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include "transport_base.h"
#include "RISTNet.h"

class rist_transport : public transport_base
{
public:
  rist_transport();
  ~rist_transport() override;

  bool initialize(const stream_config& config) override;
  bool start() override;
  void stop() override;
  void send_buffer(shared_buffer_ptr buffer, send_callback callback = nullptr) override;
  bool is_connected() const override;
  std::string get_stream_id() const override;
  transport_protocol get_protocol() const override;
  stream_stats get_stats() const override;

private:
  void stats_thread_func();
  void update_stats(const rist_stats& stats);

  RISTNetSender* m_sender;
  std::vector<std::thread> m_threads;
  std::atomic_bool m_running {false};
  std::atomic_bool m_connected {false};
  mutable std::mutex m_stats_mutex;
};
