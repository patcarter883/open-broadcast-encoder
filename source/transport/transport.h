#pragma once

#include <format>
#include <functional>
#include <memory>
#include <vector>

#include <sys/types.h>

#include "RISTNet.h"
#include "lib/lib.h"
#include "url/url.h"

using std::string;

class transport
{
public:
  void setup_rist_sender(output_config& output_c);
  void send_buffer(const std::vector<uint8_t>& data, u_int16_t connection_id);
  transport();
  ~transport();
  transport(const transport&) = delete;
  transport& operator=(const transport&) = delete;
  transport(transport&&) = delete;
  transport& operator=(transport&&) = delete;
  void set_log_callback(int (*log_callback)(void*,
                                            enum rist_log_level,
                                            const char*));
  void set_statistics_callback(void (*statistics_callback)(const rist_stats&));
  void set_oob_callback(void (*oob_callback)(const uint8_t*, size_t));

private:
  std::unique_ptr<RISTNetSender> rist_sender = std::make_unique<RISTNetSender>();
  int (*log_callback)(void*, enum rist_log_level, const char*) = nullptr;
  void (*statistics_callback)(const rist_stats&) = nullptr;
  void (*oob_callback)(const uint8_t*, size_t) = nullptr;
  void stats_cb_func(const rist_stats& stats);
  void oob_cb_func(const uint8_t* buf,
                   size_t size,
                   std::shared_ptr<RISTNetSender::NetworkConnection>& connection,
                   rist_peer* peer);
};
