#pragma once

#include <atomic>
#include <format>
#include <functional>
#include <memory>
#include <vector>

#ifndef _WIN32
#  include <sys/types.h>
#else
#  include <cstdint>
   using u_int16_t = uint16_t;
#endif

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
  // Read from the RIST sender thread (stats/OOB dispatch) and written from the
  // main/UI thread (run_loop/stop) — atomic to avoid a data race on the swap.
  std::atomic<void (*)(const rist_stats&)> statistics_callback {nullptr};
  std::atomic<void (*)(const uint8_t*, size_t)> oob_callback {nullptr};
  void stats_cb_func(const rist_stats& stats);
  void oob_cb_func(const uint8_t* buf,
                   size_t size,
                   std::shared_ptr<RISTNetSender::NetworkConnection>& connection,
                   rist_peer* peer);
};
