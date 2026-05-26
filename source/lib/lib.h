#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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
  std::vector<uint8_t> buf_data;
};

enum class bitrate_source : std::uint8_t
{
  local,
  remote_oob
};

struct __attribute__((packed)) wan_telemetry
{
  uint8_t link_quality;
  uint32_t worst_case_rtt;  // network byte order on the wire
};
static_assert(sizeof(wan_telemetry) == 5,
              "wan_telemetry must be exactly 5 bytes");

struct cumulative_stats
{
  std::deque<int> bandwidth;
  std::deque<int> retransmitted_packets;
  std::deque<int> total_packets;
  std::deque<int> encode_bitrate;
  int bandwidth_avg = 0;
  int retransmitted_packets_sum = 0;
  int total_packets_sum = 0;
  int encode_bitrate_avg = 0;
  int current_bitrate = 0;
  double previous_quality = 0.0;
  int wan_quality = 0;
  uint32_t wan_rtt = 0;
};

struct input_config
{
  std::string selected_input;
  input_mode selected_input_mode = input_mode::none;
};

struct encode_config
{
  codec selected_codec = codec::h264;
  encoder selected_encoder = encoder::software;
  int bitrate = 4300;
  bitrate_source scaling_source = bitrate_source::local;
};

struct output_config
{
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

  input_config input_cfg;
  encode_config encode_cfg;
  output_config output_cfg;

  cumulative_stats stats;

  std::shared_ptr<encode> encoder_ptr;

  void log_append(const std::string& msg) const;
};

struct app_context
{
  library lib;
  user_interface* ui = nullptr;
  std::unique_ptr<transport> transporter;
  std::unique_ptr<ndi_input> ndi;
};