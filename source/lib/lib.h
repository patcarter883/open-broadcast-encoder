#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <cstdint>
#include <exception>

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
  
  // std::future<void> input_thread_future;
  // std::future<void> encode_thread_future;
  // std::future<void> transport_thread_future;

  std::atomic_bool is_running;

  std::vector<std::thread> threads;


  input_config input_config;
  encode_config encode_config;
  output_config output_config;

  cumulative_stats stats;

  
  void log_append(const std::string &msg) const;
};