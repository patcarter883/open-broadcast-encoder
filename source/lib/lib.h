#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <string>
#include <vector>
#include <cstdint>
#include <exception>

struct encode;
struct transport;
struct ndi_input;
class user_interface;

enum class input_mode : std::uint8_t
{
  testsrc,
  mpegts,
  sdp,
  ndi,
  none
};

enum class codec : std::uint8_t
{
  h264,
  h265,
  av1
};

enum class encoder : std::uint8_t
{
  amd,
  qsv,
  nvenc,
  software
};

struct buffer_data
{
  size_t buf_size {0};
  uint8_t* buf_data {};
  uint64_t seq {0};
  uint64_t ts_ntp {0};
};

struct cumulative_stats
{
  std::vector<int> bandwidth;
  std::vector<int> retransmitted_packets;
  std::vector<int> total_packets;
  std::vector<int> encode_bitrate;
  int bandwidth_avg = 0;
  int retransmitted_packets_sum = 0;
  int total_packets_sum = 0;
  int encode_bitrate_avg = 0;
  int current_bitrate;
  double previous_quality;
};

struct input_config {
  std::string selected_input;
  input_mode selected_input_mode = input_mode::none;
};

struct encode_config {
  codec codec = codec::h264;
  encoder encoder = encoder::software;
  int bitrate = 4300;
};

struct output_config {
  std::string address = "127.0.0.1:5000";
  std::string host = "127.0.0.1";
  int port = 5000;
  int streams = 1;
  int buffer_min = 245;
  int buffer_max = 5000;
  int rtt_min = 40;
  int rtt_max = 500;
  int reorder_buffer = 240;
  int bandwidth = 6000;
};

inline std::pair<std::string, int> parse_address(const std::string& addr)
{
  auto colon = addr.find(':');
  if (colon == std::string::npos) {
    return {"127.0.0.1", 5000};
  }
  std::string h = addr.substr(0, colon);
  if (h.empty()) {
    h = "127.0.0.1";
  }
  std::string p = addr.substr(colon + 1);
  try {
    int port = std::stoi(p);
    return {h, port};
  } catch (...) {
    return {h, 5000};
  }
}

struct library
{
  /**
   * @brief Simply initializes the name member to the name of the project
   */
  library() noexcept;
  
  // std::future<void> input_thread_future;
  // std::future<void> encode_thread_future;
  // std::future<void> transport_thread_future;

  std::atomic_bool is_running {false};
   std::shared_ptr<std::atomic<bool>> run_flag;

   std::vector<std::thread> threads;


  input_config input_config;
  encode_config encode_config;
  output_config output_config;

  cumulative_stats stats;

  std::shared_ptr<encode> encoder_ptr;

  
  void log_append(const std::string &msg) const;
};

struct app_context
{
  library lib;
  user_interface* ui = nullptr;
  std::unique_ptr<transport> transporter;
  std::unique_ptr<ndi_input> ndi;
};