#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include "lib/lib.h"

class encode
{
public:
  std::atomic<bool> encoder_running;
  std::atomic<bool> pipeline_cleaned_up {false};
  void run_encode_thread();
  void stop_encode_thread();
  auto pull_video_buffer() -> buffer_data;
  auto pull_audio_buffer() -> buffer_data;
  void set_encode_bitrate(int new_bitrate);
  explicit encode(const input_config& input_config,
                  const encode_config& encode_config,
                  std::shared_ptr<std::atomic<bool>> run_flag,
                  std::function<void(const std::string&)> log_func);
  ~encode();
  encode(const encode&) = delete;
  encode& operator=(const encode&) = delete;
  encode(encode&&) = delete;
  encode& operator=(encode&&) = delete;

private:
  // Guards video_encoder/audio_sink/video_sink/bus/datasrc_pipeline pointers
  // and serialises clear_pipeline_state() against pull_*_buffer/set_encode_bitrate.
  std::mutex pipeline_mutex;
  std::vector<std::thread> threads;
  std::shared_ptr<std::atomic<bool>> run_flag;
  std::function<void(const std::string&)> log_func;
  const input_config& input_c;
  const encode_config& encode_c;
  std::string pipeline_str;
  GstElement* datasrc_pipeline = nullptr;
  GstElement* video_encoder = nullptr;
  GstElement* audio_sink = nullptr;
  GstElement* video_sink = nullptr;
  GstBus* bus = nullptr;
  void clear_pipeline_state();
  auto pull_from_sink(GstElement* encode::* sink_field) -> buffer_data;
  void build_pipeline();
  void pipeline_build_source();
  void pipeline_build_sink();
  void pipeline_build_video_demux();
  void pipeline_build_audio_demux();
  void pipeline_build_audio_encoder();
  void pipeline_build_video_encoder();
  void pipeline_build_audio_payloader();
  void pipeline_build_video_payloader();
  void parse_pipeline();
  void play_pipeline();
  void handle_gst_message_error(GstMessage* message);
  void handle_gst_message_eos(GstMessage* message);
  void handle_gstreamer_message(GstMessage* message);
  void log(const std::string& msg) const;
};