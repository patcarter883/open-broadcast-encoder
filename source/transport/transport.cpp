#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "transport/transport.h"

#include "RISTNet.h"

using std::string;

transport::transport()
{
  this->rist_sender->statisticsCallback =
      std::bind_front(&transport::stats_cb_func, this);
  this->rist_sender->networkOOBDataCallback =
      std::bind_front(&transport::oob_cb_func, this);
}

transport::~transport()
{
  this->statistics_callback = nullptr;
  this->oob_callback = nullptr;
  this->wait_callbacks_drained();
  this->rist_sender->closeAllClientConnections();
  this->rist_sender->destroySender();
}

void transport::set_log_callback(int (*log_callback_func)(void*,
                                                          enum rist_log_level,
                                                          const char*))
{
  this->log_callback = log_callback_func;
}

void transport::set_statistics_callback(
    void (*statistics_callback_func)(const rist_stats&))
{
  this->statistics_callback = statistics_callback_func;
}

void transport::set_oob_callback(void (*oob_callback_func)(const uint8_t*,
                                                           size_t))
{
  this->oob_callback = oob_callback_func;
}

void transport::wait_callbacks_drained()
{
  while (this->in_flight_callbacks.load(std::memory_order_acquire) > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void transport::stats_cb_func(const rist_stats& stats)
{
  this->in_flight_callbacks.fetch_add(1, std::memory_order_acq_rel);
  auto* cb = this->statistics_callback;
  if (cb != nullptr) {
    cb(stats);
  }
  this->in_flight_callbacks.fetch_sub(1, std::memory_order_acq_rel);
}

void transport::oob_cb_func(
    const uint8_t* buf,
    size_t size,
    std::shared_ptr<RISTNetSender::NetworkConnection>& /*connection*/,
    rist_peer* /*peer*/)
{
  this->in_flight_callbacks.fetch_add(1, std::memory_order_acq_rel);
  auto* cb = this->oob_callback;
  if (cb != nullptr) {
    cb(buf, size);
  }
  this->in_flight_callbacks.fetch_sub(1, std::memory_order_acq_rel);
}

void transport::setup_rist_sender(output_config& output_c)
{
  if (output_c.streams < 1) {
    output_c.streams = 1;
  }
  output_c.port = std::clamp(output_c.port, 1, 65535);
  if (output_c.bandwidth < 100) {
    output_c.bandwidth = 100;
  }
  if (output_c.host.empty()) {
    output_c.host = "127.0.0.1";
  }

  RISTNetSender::RISTNetSenderSettings my_send_configuration;

  std::vector<std::tuple<string, int>> interface_list_sender;

  for (int i = 0; i < output_c.streams; i = i + 1) {
    string rist_output_url = std::format(
        "rist://"
        "{}:{}?bandwidth={}&buffer-min={}&buffer-max={}&rtt-min={}&rtt-max={}&"
        "reorder-buffer={}&timing-mode=2",
        output_c.host,
        output_c.port + (2 * i),
        output_c.bandwidth,
        output_c.buffer_min,
        output_c.buffer_max,
        output_c.rtt_min,
        output_c.rtt_max,
        output_c.reorder_buffer);

    interface_list_sender.emplace_back(rist_output_url, 0);
  }

  my_send_configuration.mLogLevel = RIST_LOG_DEBUG;
  my_send_configuration.mProfile = RIST_PROFILE_ADVANCED;

  my_send_configuration.mLogSetting->log_cb = log_callback;
  this->rist_sender->initSender(interface_list_sender, my_send_configuration);
}

void transport::send_buffer(const std::vector<uint8_t>& data,
                            u_int16_t virt_dst_port)
{
  this->rist_sender->sendData(data.data(), data.size(), 0, virt_dst_port);
}
