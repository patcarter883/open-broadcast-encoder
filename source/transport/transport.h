#pragma once

#include <sys/types.h>
#include <functional>
#include <format>
#include <memory>
#include <vector>
#include "url/url.h"
#include "RISTNet.h"
#include "lib/lib.h"

using std::string;

class transport
{
public:
  void setup_rist_sender(output_config &output_c);
  void send_buffer(buffer_data &buf, u_int16_t connection_id);
  transport();
  ~transport();
  void set_log_callback(int (*log_callback)(void*, enum rist_log_level, const char*));
  void set_statistics_callback(void (*statistics_callback)(const rist_stats&));

private:
  RISTNetSender* rist_sender = new RISTNetSender();
  int (*log_callback)(void*, enum rist_log_level, const char*) = nullptr;
  void (*statistics_callback)(const rist_stats&) = nullptr;
  void stats_cb_func(const rist_stats& stats);
};