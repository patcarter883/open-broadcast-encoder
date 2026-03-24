#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <future>
#include <string>
#include <vector>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include "lib/lib.h"

using log_func_ptr = void (*)(const std::string& msg);

class encode
{
public:
  std::atomic<bool> encoder_running;
  void run_encode_thread();
  void stop_encode_thread();
  auto pull_video_buffer() -> buffer_data;
  auto pull_audio_buffer() -> buffer_data;
  void set_encode_bitrate(int new_bitrate);
  explicit encode(const input_config& input_config,
                  const encode_config& encode_config,
                  std::atomic_bool* run_flag,
                  log_func_ptr log_func);
  ~encode();

private:
  std::vector<std::thread> threads;
  std::atomic_bool* run_flag;
  log_func_ptr log_func = nullptr;
  const input_config& input_c;
  const encode_config& encode_c;
  std::string pipeline_str;
  GstElement* datasrc_pipeline = nullptr;
  GstElement* video_encoder = nullptr;
  GstElement* audio_sink = nullptr;
  GstElement* video_sink = nullptr;
  GstBus* bus = nullptr;
  void build_pipeline();
  void pipeline_build_source();
  void pipeline_build_sink();
  void pipeline_build_test_source();
  void pipeline_build_video_demux();
  void pipeline_build_audio_demux();
  void pipeline_build_audio_encoder();
  void pipeline_build_video_encoder();
  void pipeline_build_amd_encoder();
  void pipeline_build_qsv_encoder();
  void pipeline_build_nvenc_encoder();
  void pipeline_build_software_encoder();
  void pipeline_build_amd_h264_encoder();
  void pipeline_build_amd_h265_encoder();
  void pipeline_build_amd_av1_encoder();
  void pipeline_build_qsv_h264_encoder();
  void pipeline_build_qsv_h265_encoder();
  void pipeline_build_qsv_av1_encoder();
  void pipeline_build_nvenc_h264_encoder();
  void pipeline_build_nvenc_h265_encoder();
  void pipeline_build_nvenc_av1_encoder();
  void pipeline_build_software_h264_encoder();
  void pipeline_build_software_h265_encoder();
  void pipeline_build_software_av1_encoder();
  void pipeline_build_audio_payloader();
  void pipeline_build_video_payloader();
  void parse_pipeline();
  void play_pipeline();
  void handle_gst_message_error(GstMessage* message);
  void handle_gst_message_eos(GstMessage* message);
  void handle_gstreamer_message(GstMessage* message);
  void log(const std::string& msg) const;
};