module;
#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <future>
#include <string>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

// #include "common.h"

export module encode;
import library;

using log_func_ptr = void (*)(const std::string& msg);

export class encode
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

using std::string;

encode::encode(const input_config& input_config,
               const encode_config& encode_config,
               std::atomic_bool* run_flag,
               log_func_ptr log_func)
    : encoder_running {std::atomic<bool>(false)}
    , run_flag {run_flag}
    , log_func {log_func}
    , input_c {input_config}
    , encode_c {encode_config}
{
}

encode::~encode()
{
  gst_element_set_state(this->datasrc_pipeline, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(this->datasrc_pipeline));
  gst_object_unref(this->bus);
  log("Stopping pipeline.\n");

  for (auto& t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }
}

void encode::pipeline_build_source()
{
  switch (input_c.selected_input_mode) {
    case input_mode::mpegts:
      this->pipeline_str = std::format(
          "udpsrc port={} ! tsparse set-timestamps=true ! tsdemux latency=10 "
          "name=demux ",
          input_c.selected_input);
      break;
    case input_mode::sdp:
    case input_mode::ndi:
      this->pipeline_str = std::format(
          "ndisrc do-timestamp=true ndi-name=\"{}\" ! ndisrcdemux name=demux ",
          input_c.selected_input);
      break;
    case input_mode::none:
      break;
  }
}

void encode::pipeline_build_sink()
{
  this->pipeline_str +=
      " appsink name=video_sink "
      // " appsink name=audio_sink  ";
      "mpegtsmux alignment=7 name=tsmux ! video_sink. ";
}

void encode::pipeline_build_video_demux()
{
  switch (input_c.selected_input_mode) {
    case input_mode::ndi:
      this->pipeline_str += " demux.video ! queue silent=true ! videoconvert !";
      break;
    default:
      this->pipeline_str +=
          " demux. ! queue silent=true ! decodebin3 ! videoconvert !";
      break;
  }
}

void encode::pipeline_build_audio_demux()
{
  switch (input_c.selected_input_mode) {
    case input_mode::ndi:
      this->pipeline_str +=
          " demux.audio ! queue silent=true ! audioresample ! audioconvert !";
      break;
    default:
      this->pipeline_str +=
          " demux. ! queue silent=true ! decodebin3 ! audioresample ! "
          "audioconvert !";
      break;
  }
}

void encode::pipeline_build_audio_encoder()
{
  this->pipeline_str += " avenc_aac ! aacparse ";
}

void encode::pipeline_build_video_encoder()
{
  switch (encode_c.encoder) {
    case encoder::amd:
      pipeline_build_amd_encoder();
      break;

    case encoder::qsv:
      pipeline_build_qsv_encoder();
      break;

    case encoder::nvenc:
      pipeline_build_nvenc_encoder();
      break;

    default:
      pipeline_build_software_encoder();
      break;
  }
}

void encode::pipeline_build_amd_encoder()
{
  switch (encode_c.codec) {
    case codec::h265:
      pipeline_build_amd_h265_encoder();
      break;

    case codec::av1:
      pipeline_build_amd_av1_encoder();
      break;

    default:
      pipeline_build_amd_h264_encoder();
      break;
  }
}

void encode::pipeline_build_qsv_encoder()
{
  switch (encode_c.codec) {
    case codec::h265:
      pipeline_build_qsv_h265_encoder();
      break;

    case codec::av1:
      pipeline_build_qsv_av1_encoder();
      break;

    default:
      pipeline_build_qsv_h264_encoder();
      break;
  }
}

void encode::pipeline_build_nvenc_encoder()
{
  switch (encode_c.codec) {
    case codec::h265:
      pipeline_build_nvenc_h265_encoder();
      break;

    case codec::av1:
      pipeline_build_nvenc_av1_encoder();
      break;

    default:
      pipeline_build_nvenc_h264_encoder();
      break;
  }
}

void encode::pipeline_build_software_encoder()
{
  switch (encode_c.codec) {
    case codec::h265:
      pipeline_build_software_h265_encoder();
      break;

    case codec::av1:
      pipeline_build_software_av1_encoder();
      break;

    default:
      pipeline_build_software_h264_encoder();
      break;
  }
}

void encode::pipeline_build_amd_h264_encoder()
{
  this->pipeline_str += std::format(
      "amfh264enc name=videncoder  bitrate={} rate-control=cbr "
      "usage=low-latency preset=quality pre-encode=true pa-hqmb-mode=auto ! "
      "video/x-h264,framerate=60/1,profile=high ! h264parse config-interval=1 ",
      encode_c.bitrate);
}

void encode::pipeline_build_amd_h265_encoder()
{
  this->pipeline_str += std::format(
      "amfh265enc name=videncoder bitrate={} rate-control=cbr "
      "usage=low-latency preset=quality pre-encode=true pa-hqmb-mode=auto ! "
      "video/x-h265,framerate=60/1 ! h264parse config-interval=1 ",
      encode_c.bitrate);
}

void encode::pipeline_build_amd_av1_encoder()
{
  this->pipeline_str += std::format(
      "amfav1enc name=videncoder bitrate={} rate-control=cbr "
      "usage=low-latency preset=high-quality  pre-encode=true "
      "pa-hqmb-mode=auto ! video/x-av1,framerate=60/1 "
      "! av1parse ",
      encode_c.bitrate);
}

void encode::pipeline_build_qsv_h264_encoder()
{
  this->pipeline_str += std::format(
      "qsvh264enc name=videncoder  bitrate={} rate-control=cbr "
      "target-usage=1 ! video/x-h264,framerate=60/1  ! h264parse "
      "config-interval=1 ",
      encode_c.bitrate);
}

void encode::pipeline_build_qsv_h265_encoder()
{
  this->pipeline_str += std::format(
      "qsvh265enc name=videncoder bitrate={} rate-control=cbr "
      "target-usage=1 ! video/x-h265,framerate=60/1  ! h264parse "
      "config-interval=1 ",
      encode_c.bitrate);
}

void encode::pipeline_build_qsv_av1_encoder()
{
  this->pipeline_str += std::format(
      "qsvav1enc name=videncoder bitrate={} rate-control=cbr "
      "target-usage=1 gop-size=120 ! video/x-av1,framerate=60/1 ! av1parse ",
      encode_c.bitrate);
}

void encode::pipeline_build_nvenc_h264_encoder()
{
  this->pipeline_str += std::format(
      "nvh264enc name=videncoder bitrate={} rc-mode=cbr-hq "
      "preset=low-latency-hq ! h264parse config-interval=1 ",
      encode_c.bitrate);
}

void encode::pipeline_build_nvenc_h265_encoder()
{
  this->pipeline_str += std::format(
      "nvh265enc name=videncoder bitrate={} rc-mode=cbr-hq "
      "preset=low-latency-hq ! h264parse config-interval=1 ",
      encode_c.bitrate);
}

void encode::pipeline_build_nvenc_av1_encoder()
{
  this->pipeline_str += std::format(
      "x264enc name=videncoder speed-preset=fast tune=zerolatency "
      "bitrate={} ! h264parse config-interval=1 ",
      encode_c.bitrate);
}

void encode::pipeline_build_software_h264_encoder()
{
  this->pipeline_str += std::format(
      "x264enc name=videncoder bitrate={} "
      "speed-preset=fast tune=zerolatency ! h264parse config-interval=1 ",
      encode_c.bitrate);
}

void encode::pipeline_build_software_h265_encoder()
{
  this->pipeline_str += std::format(
      "x265enc name=videncoder bitrate={} "
      "speed-preset=fast tune=zerolatency ! h264parse config-interval=1 ",
      encode_c.bitrate);
}

void encode::pipeline_build_software_av1_encoder()
{
  this->pipeline_str += std::format(
      "rav1enc name=videncoder bitrate={} speed-preset=8 tile-cols=2 "
      "tile-rows=2 ! av1parse ",
      encode_c.bitrate);
}

void encode::pipeline_build_audio_payloader()
{
  this->pipeline_str += "! queue silent=true ! tsmux. ";
}

void encode::pipeline_build_video_payloader()
{
  std::string payloader;

  switch (encode_c.codec) {
    case codec::h265:
      payloader = "rtph265pay";
      break;

    case codec::av1:
      payloader = "rtpav1pay";
      break;

    default:
      payloader = "rtph264pay";
      break;
  }

  this->pipeline_str += "! queue silent=true ! tsmux. ";
}

void encode::build_pipeline()
{
  this->pipeline_build_source();
  this->pipeline_build_sink();
  this->pipeline_build_audio_demux();
  this->pipeline_build_audio_encoder();
  this->pipeline_build_audio_payloader();
  this->pipeline_build_video_demux();
  this->pipeline_build_video_encoder();
  this->pipeline_build_video_payloader();
}

void encode::parse_pipeline()
{
  this->datasrc_pipeline = nullptr;
  GError* error = nullptr;

  log(this->pipeline_str);

  this->datasrc_pipeline = gst_parse_launch(this->pipeline_str.c_str(), &error);
  if (error != nullptr) {
    log(std::format("Parse Error: {}", error->message));
    g_clear_error(&error);
  }
  if (this->datasrc_pipeline == nullptr) {
    log("*** Bad datasrc_pipeline ***\n");
  }

  this->video_encoder =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "videncoder");
  this->video_sink =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "video_sink");
  this->audio_sink =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "audio_sink");

  this->bus = gst_element_get_bus(this->datasrc_pipeline);
}

void encode::play_pipeline()
{
  encoder_running = true;
  std::chrono::milliseconds duration(1);
  while (*run_flag) {
    GstMessage* msg = gst_bus_timed_pop(this->bus, GST_MSECOND);
    if (msg != nullptr) {
      this->handle_gstreamer_message(msg);
      gst_message_unref(msg);
    } else {
      std::this_thread::yield();
      std::this_thread::sleep_for(duration);
    }
  }
}

void encode::run_encode_thread()
{
  this->build_pipeline();
  this->parse_pipeline();
  log("Playing pipeline.\n");
  gst_element_set_state(this->datasrc_pipeline, GST_STATE_PLAYING);
  threads.emplace_back([&] { play_pipeline(); });
}

void encode::stop_encode_thread()
{
  gst_element_set_state(this->datasrc_pipeline, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(this->datasrc_pipeline));
  gst_object_unref(this->bus);
  log("Stopping pipeline.\n");

  encoder_running = false;

  // std::future_status status;

  // if (app.encode_thread_future->valid())
  // {
  //     switch (status =
  //     app.encode_thread_future->wait_for(std::chrono::seconds(1)); status)
  //     {
  //     case std::future_status::timeout:
  //         log("Waiting for encoder stop has timed out.");
  //         break;
  //     case std::future_status::ready:
  //         log("encoder stopped.");
  //         break;
  //     }
  // }
}

void encode::handle_gst_message_error(GstMessage* message)
{
  GError* err;
  gchar* debug_info;
  gst_message_parse_error(message, &err, &debug_info);
  log("\nReceived error from datasrc_pipeline...\n");
  log(std::format("Error received from element {}: {}\n",
                  GST_OBJECT_NAME(message->src),
                  err->message));
  log(std::format("Debugging information: {}\n",
                  (debug_info != nullptr) ? debug_info : "none"));
  g_clear_error(&err);
  g_free(debug_info);
  encoder_running = false;
}

void encode::handle_gst_message_eos(GstMessage* /*message*/)
{
  log("\nReceived EOS from pipeline...\n");
  encoder_running = false;
}

void encode::handle_gstreamer_message(GstMessage* message)
{
  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR:
      this->handle_gst_message_error(message);
      break;
    case GST_MESSAGE_EOS:
      this->handle_gst_message_eos(message);
      break;
    default:
      break;
  }
}

auto encode::pull_video_buffer() -> buffer_data
{
  GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(this->video_sink));
  GstBuffer* buffer = gst_sample_get_buffer(sample);

  if (buffer != nullptr) {
    GstMapInfo info;
    gst_buffer_map(buffer, &info, GST_MAP_READ);
    gst_sample_unref(sample);
    return buffer_data {.buf_size = info.size, .buf_data = info.data};
  }

  gst_sample_unref(sample);
  return buffer_data {};
}

auto encode::pull_audio_buffer() -> buffer_data
{
  GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(this->audio_sink));
  GstBuffer* buffer = gst_sample_get_buffer(sample);

  if (buffer != nullptr) {
    GstMapInfo info;
    gst_buffer_map(buffer, &info, GST_MAP_READ);
    gst_sample_unref(sample);
    return buffer_data {.buf_size = info.size, .buf_data = info.data};
  }

  gst_sample_unref(sample);
  return buffer_data {};
}

void encode::set_encode_bitrate(int new_bitrate)
{
  g_object_set(G_OBJECT(this->video_encoder), "bitrate", new_bitrate, NULL);
}

void encode::log(const std::string& msg) const
{
  if (log_func != nullptr) {
    log_func(msg);
  }
}
