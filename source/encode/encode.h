// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include "encode/fps_snap.h"
#include "lib/lib.h"

class raw_local_input;
class capture_input;

// Lifecycle of the encode pipeline. Without an explicit state a parse failure
// or a pipeline error leaves the app looking identical to a healthy stream:
// the twist is that `encoder_running` stops play_pipeline() but nothing in the
// app's own send loop reads it, so it kept spinning on an empty sink while the
// window still read "Start Encode" pressed. `failed` is terminal until the
// operator Stops and Starts again.
enum class encode_state
{
  idle,
  starting,
  streaming,
  failed
};

auto encode_state_text(encode_state state) -> const char*;

class encode
{
public:
  std::atomic<bool> encoder_running;
  std::atomic<bool> pipeline_cleaned_up {false};
  // Set by this class on every transition and read by the app's send loop and
  // the UI (see run_loop / user_interface::set_encode_state).
  std::atomic<encode_state> state {encode_state::idle};
  void run_encode_thread();
  void stop_encode_thread();
  auto pull_video_buffer() -> buffer_data;
  auto pull_audio_buffer() -> buffer_data;
  void set_encode_bitrate(int new_bitrate);
  // The rate this encoder is running at, snapped to a standard broadcast rate
  // (24/25/30/50/60), or zero when nothing has been measured yet -- a caller
  // then leaves a rate-dependent value alone rather than guessing at one.
  //
  // No input configuration or setting carries a rate, so the measurement can
  // only happen where a pipeline is live: the encode pipeline's caps negotiate
  // when it starts, and the raw/OBS header arrives when OBS connects. The first
  // result is cached, because Allocate and Start Encode are independent UI
  // actions -- the measurement and its use are different moments.
  std::uint32_t source_fps_snapped();
  explicit encode(const input_config& input_config,
                  const encode_config& encode_config,
                  const receiver_control_config& receiver_config,
                  std::shared_ptr<std::atomic<bool>> run_flag,
                  std::function<void(const std::string&)> log_func);
  ~encode();
  encode(const encode&) = delete;
  encode& operator=(const encode&) = delete;
  encode(encode&&) = delete;
  encode& operator=(encode&&) = delete;

private:
  // Guards video_encoder/audio_sink/video_sink/bus/datasrc_pipeline pointers
  // and serialises clear_pipeline_state() against
  // pull_*_buffer/set_encode_bitrate.
  std::mutex pipeline_mutex;
  std::vector<std::thread> threads;
  std::shared_ptr<std::atomic<bool>> run_flag;
  // The snapped rate once measured. Written by whichever thread first measures
  // (the encode pipeline's own start); read by the allocation worker --
  // different threads, so atomic. Never cleared: a measurement is not
  // invalidated by a later pipeline stop, and the rate of a live input does not
  // change.
  std::atomic<std::uint32_t> cached_snapped_fps {0};
  std::function<void(const std::string&)> log_func;
  const input_config& input_c;
  const encode_config& encode_c;
  // Read for the raw-capture policy only: a destination that cannot carry the
  // announced format earns a warning (see raw_format_verdict).
  const receiver_control_config& receiver_c;
  std::string pipeline_str;
  GstElement* datasrc_pipeline = nullptr;
  GstElement* video_encoder = nullptr;
  GstElement* audio_sink = nullptr;
  GstElement* video_sink = nullptr;
  // Branch entry queues for the MPEG-TS demux. That demux's pads are dynamic,
  // and tsdemux cannot resolve two any-pad (`demux.`) delayed links on
  // GStreamer 1.28.6, so the branches are linked by caps from a pad-added
  // handler instead (see link_demux_pads).
  GstElement* video_queue = nullptr;
  GstElement* audio_queue = nullptr;
  // raw_local only: the two appsrcs fed by raw_local_input, and the reader
  // itself. Owned refs come from gst_bin_get_by_name in parse_pipeline(); the
  // reader is stopped before they are unreffed (see clear_pipeline_state).
  GstElement* raw_video_src = nullptr;
  GstElement* raw_audio_src = nullptr;
  std::unique_ptr<raw_local_input> raw_reader;
  // jpegxs_capture only (MC4): the `capturets` appsrc the LAN reader pushes
  // MPEG-TS datagrams into, and the reader itself. Stopped before the appsrc is
  // unreffed (see clear_pipeline_state), exactly like the raw_local reader.
  GstElement* capture_ts_src = nullptr;
  std::unique_ptr<capture_input> capture_reader;
  GstBus* bus = nullptr;
  void clear_pipeline_state();
  auto pull_from_sink(GstElement* encode::*sink_field) -> buffer_data;
  void build_pipeline();
  void pipeline_build_source();
  void pipeline_build_sink();
  void pipeline_build_video_demux();
  void pipeline_build_audio_demux();
  void pipeline_build_audio_encoder();
  void pipeline_build_video_encoder();
  void pipeline_build_audio_payloader();
  void pipeline_build_video_payloader();
  void link_demux_pads();
  void link_demux_pad(GstPad* pad);
  static void on_demux_pad_added(GstElement* demux,
                                 GstPad* pad,
                                 gpointer user_data);
  void parse_pipeline();
  void play_pipeline();
  void start_raw_reader();
  void start_capture_reader();
  auto raw_format_verdict(std::uint32_t obs_format) -> std::string;
  void handle_gst_message_error(GstMessage* message);
  void handle_gst_message_eos(GstMessage* message);
  void handle_gstreamer_message(GstMessage* message);
  void log(const std::string& msg) const;
};