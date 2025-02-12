module;
#include <sys/types.h>
#include <functional>
#include <format>
#include <memory>
#include <vector>
#include "url.h"
#include "RISTNet.h"
// #include "common.h"

export module transport;
import library;
using std::string;
using homer6::url;


export class transport
{
public:
  void setup_rist_sender(output_config &output_c);
  void send_buffer(buffer_data &buf, u_int16_t connection_id);
  transport();

  ~transport();
  // transport(const transport& other)  // copy constructor
  //     : transport(other)
  // {
  // }

  // transport(transport&& other) noexcept  // move constructor
  //     : app(std::exchange(other.app, nullptr))
  // {
  // }

  // transport& operator=(const transport& other)  // copy assignment
  // {
  //   return *this = transport(other);
  // }

  // transport& operator=(transport&& other) noexcept  // move assignment
  // {
  //   std::swap(app, other.app);
  //   return *this;
  // }
  void set_log_callback(int (*log_callback)(void*, enum rist_log_level, const char*));
  void set_statistics_callback(void (*statistics_callback)(const rist_stats&));

private:
  RISTNetSender* rist_sender = new RISTNetSender();
  int (*log_callback)(void*, enum rist_log_level, const char*) = nullptr;
  void (*statistics_callback)(const rist_stats&) = nullptr;
  void stats_cb_func(const rist_stats& stats);
};

transport::transport()
  {
      this->rist_sender->statisticsCallback = std::bind_front(&transport::stats_cb_func, this);
  }

transport::~transport() { 
  this->rist_sender->closeAllClientConnections();
  this->rist_sender->destroySender(); 
  }

void transport::set_log_callback(int (*log_callback_func)(void*, enum rist_log_level, const char*))
{
  this->log_callback = log_callback_func;
}

void transport::set_statistics_callback(void (*statistics_callback_func)(const rist_stats&))
{
  this->statistics_callback = statistics_callback_func;
}

void transport::stats_cb_func(const rist_stats& stats)
{
    if (this->statistics_callback != nullptr) {
        this->statistics_callback(stats);
    }
}

void transport::setup_rist_sender(output_config &output_c) {

  RISTNetSender::RISTNetSenderSettings my_send_configuration;

  std::vector<std::tuple<string, int>> interface_list_sender;

  url url{ std::format("rist://{}", output_c.address) };

  for (int i = 0; i < output_c.streams; i = i + 1)
  {
      string rist_output_url = std::format(
          "rist://"
          "{}:{}?bandwidth={}buffer-min={}&buffer-max={}&rtt-min={}&rtt-max={}&"
          "reorder-buffer={}&timing-mode=2",
          url.getHost(),
          url.getPort() + (2 * i),
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

void transport::send_buffer(buffer_data &buf, u_int16_t virt_dst_port=0)
{
    this->rist_sender->sendData(buf.buf_data, buf.buf_size, 0, virt_dst_port); //, buf.seq, buf.ts_ntp);
}
