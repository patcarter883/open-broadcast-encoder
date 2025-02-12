
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
enum class input_mode
{
  sdp,
  ndi,
  none
};

enum class codec
{
  h264,
  h265,
  av1
};

enum class encoder
{
  amd,
  qsv,
  nvenc,
  software
};

struct BufferData
{
  size_t buf_size {0};
  uint8_t* buf_data {};
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