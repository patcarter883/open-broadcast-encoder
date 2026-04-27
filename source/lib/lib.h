#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <cstdint>
#include <exception>
#include <memory>

enum class input_mode : std::uint8_t
{
  mpegts,
  sdp,
  ndi,
  test,
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

enum class transport_protocol : std::uint8_t
{
  rist,
  srt,
  rtmp
};

struct buffer_data
{
  size_t buf_size {0};
  uint8_t* buf_data {};
  uint64_t seq {0};
  uint64_t ts_ntp {0};
};

/**
 * @brief Shared buffer for multi-consumer distribution
 * @details Uses shared_ptr to allow multiple transports to reference
 *         the same buffer data without copy-on-write overhead
 */
struct shared_buffer
{
  std::shared_ptr<uint8_t[]> data;
  size_t size {0};
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
  int current_bitrate = 0;
  double previous_quality;
};

/**
 * @brief Per-stream statistics for multi-output support
 * @details Tracks statistics for each individual output stream
 */
struct stream_stats
{
  std::string stream_id;
  transport_protocol protocol = transport_protocol::rist;
  int64_t bandwidth = 0;
  int64_t retransmitted_packets = 0;
  int64_t total_packets = 0;
  double quality = 100.0;
  int32_t rtt = 0;
  bool connected = false;
  std::chrono::steady_clock::time_point last_update;
};

/**
 * @brief Configuration for a single output stream
 * @details Defines protocol-specific settings for each output destination
 */
struct stream_config
{
  std::string id;
  transport_protocol protocol = transport_protocol::rist;
  std::string address;
  int streams = 1;
  std::string buffer_min = "245";
  std::string buffer_max = "5000";
  std::string rtt_min = "40";
  std::string rtt_max = "500";
  std::string reorder_buffer = "240";
  std::string bandwidth = "6000";
  // SRT-specific settings
  std::string latency = "120";
  // RTMP-specific settings
  std::string stream_key;
};

struct input_config {
  std::string selected_input;
  input_mode selected_input_mode = input_mode::none;
};

struct encode_config {
  codec codec = codec::h264;
  encoder encoder = encoder::software;
  std::string bitrate = "4300";
  int width = 1920;
  int height = 1080;
  int framerate = 30;
};

struct output_config {
  std::string address = "127.0.0.1:5000";
  int streams = 1;
  std::string buffer_min = "245";
  std::string buffer_max = "5000";
  std::string rtt_min = "40";
  std::string rtt_max = "500";
  std::string reorder_buffer = "240";
  std::string bandwidth = "6000";
};

struct library
{
  /**
   * @brief Simply initializes the name member to the name of the project
   */
  library() noexcept;

  std::atomic_bool is_running;

  std::vector<std::thread> threads;


  input_config input_config;
  encode_config encode_config;
  output_config output_config;

  cumulative_stats stats;

  /**
   * @brief Vector of output stream configurations for multi-output support
   */
  std::vector<stream_config> streams;

  
  void log_append(const std::string &msg) const;
};

inline int safe_parse_int(const std::string& s, int default_val)
{
  try {
    size_t pos;
    int val = std::stoi(s, &pos);
    if (pos != s.size()) return default_val;
    return val;
  } catch (const std::exception&) {
    return default_val;
  }
}