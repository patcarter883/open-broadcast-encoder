#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "encode/encode.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

using std::string;

encode::encode(const input_config& input_config,
               const encode_config& encode_config,
               std::shared_ptr<std::atomic<bool>> run_flag,
               std::function<void(const std::string&)> log_func)
    : encoder_running {false}
    , run_flag {std::move(run_flag)}
    , log_func {std::move(log_func)}
    , input_c {input_config}
    , encode_c {encode_config}
{
}

encode::~encode()
{
  if (this->run_flag) {
    *this->run_flag = false;
  }
  this->encoder_running = false;

  for (auto& t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  this->clear_pipeline_state();
}

void encode::clear_pipeline_state()
{
  std::lock_guard<std::mutex> guard(this->pipeline_mutex);
  if (this->pipeline_cleaned_up.load(std::memory_order_acquire)) {
    return;
  }

  if (this->datasrc_pipeline != nullptr) {
    gst_element_set_state(this->datasrc_pipeline, GST_STATE_NULL);
  }

  // Unref in reverse-dependency order. Elements first (each holds a ref from
  // gst_bin_get_by_name), bus next, then the pipeline itself.
  if (this->video_encoder != nullptr) {
    gst_object_unref(this->video_encoder);
    this->video_encoder = nullptr;
  }
  if (this->video_sink != nullptr) {
    gst_object_unref(this->video_sink);
    this->video_sink = nullptr;
  }
  if (this->audio_sink != nullptr) {
    gst_object_unref(this->audio_sink);
    this->audio_sink = nullptr;
  }
  if (this->bus != nullptr) {
    gst_object_unref(this->bus);
    this->bus = nullptr;
  }
  if (this->datasrc_pipeline != nullptr) {
    gst_object_unref(GST_OBJECT(this->datasrc_pipeline));
    this->datasrc_pipeline = nullptr;
    log("Stopping pipeline.\n");
  }

  this->pipeline_cleaned_up.store(true, std::memory_order_release);
}

void encode::pipeline_build_source()
{
  const auto* const sdp =
      "v=0\n"
      "o=- 1443716955 1443716955 IN IP4 127.0.0.1\n"
      "s=st2110 stream\n"
      "t=0 0\n"
      "a=recvonly\n"
      "\n"
      "m=video 20000 RTP/AVP 102\n"
      "c=IN IP4 127.0.0.1/8\n"
      "a=rtpmap:102 raw/90000\n"
      "a=fmtp:102 sampling=YCbCr-4:2:2; width=1920; height=1080; "
      "exactframerate=30000/1000; depth=10; TCS=SDR; colorimetry=BT709; "
      "PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN;\n"
      "a=mediaclk:direct=0\n"
      "a=ts-refclk:ptp=IEEE1588-2008:00-02-c5-ff-fe-21-60-5c:127\n";

  switch (input_c.selected_input_mode) {
    case input_mode::testsrc:
      this->pipeline_str = "";
      break;
    case input_mode::mpegts:
      this->pipeline_str = std::format(
          "udpsrc port={} buffer-size=1000000 mtu=45000 ! tsparse "
          "set-timestamps=true ! tsdemux latency=10 "
          "name=demux ",
          input_c.selected_input);
      break;
    case input_mode::sdp:

      this->pipeline_str = std::format(
          "sdpsrc sdp=\"{}\" "
          "name=demux ",
          sdp);
      break;
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
      // enable-custom-mappings=true is REQUIRED to mux AV1 (and VP9): GStreamer
      // has no standardised MPEG-TS stream type for them, so mpegtsmux otherwise
      // fails with "AV1 requires enabling custom mapping". No-op for H.264/H.265.
      "mpegtsmux alignment=7 enable-custom-mappings=true name=tsmux "
      "! video_sink. ";
}

void encode::pipeline_build_video_demux()
{
  switch (input_c.selected_input_mode) {
    case input_mode::testsrc:
      this->pipeline_str += " videotestsrc is-live=true pattern=smpte ! videoconvert !";
      break;
    case input_mode::ndi:
      this->pipeline_str += " demux.video ! queue silent=true ! videoconvert !";
      break;
    case input_mode::sdp:
      this->pipeline_str +=
          " demux. ! rtpvrawdepay ! queue silent=true ! videoconvert !";
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
    case input_mode::testsrc:
      this->pipeline_str += " audiotestsrc is-live=true wave=sine ! audioconvert ! audioresample !";
      break;
    case input_mode::ndi:
      this->pipeline_str +=
          " demux.audio ! queue silent=true ! audioresample ! audioconvert !";
      break;
    case input_mode::sdp:
      this->pipeline_str +=
          " demux. ! rtpL24depay ! queue silent=true ! audioresample ! "
          "audioconvert !";
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

namespace
{
// Video-encoder fragment templates indexed by [encoder][codec].
//
// Row order follows enum class encoder: amd, qsv, nvenc, software.
// Column order follows enum class codec: h264, h265, av1.
//
// Each template is byte-for-byte identical to the string the corresponding
// pipeline_build_<vendor>_<codec>_encoder() function previously emitted
// (including incidental double-spaces). The single "{}" is the bitrate.
constexpr int kEncoderCount = 4;
constexpr int kCodecCount = 3;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
constexpr std::string_view kEncoderTemplates[kEncoderCount][kCodecCount] = {
    // encoder::amd
    {
        // codec::h264
        "amfh264enc name=videncoder  bitrate={} rate-control=cbr "
        "usage=low-latency preset=quality pre-encode=true pa-hqmb-mode=auto ! "
        "video/x-h264,framerate=60/1,profile=high ! h264parse "
        "config-interval=1 ",
        // codec::h265
        "amfh265enc name=videncoder bitrate={} rate-control=cbr "
        "usage=low-latency preset=quality pre-encode=true pa-hqmb-mode=auto ! "
        "video/x-h265,framerate=60/1 ! h265parse config-interval=1 ",
        // codec::av1
        "amfav1enc name=videncoder bitrate={} rate-control=cbr "
        "usage=low-latency preset=high-quality  pre-encode=true "
        "pa-hqmb-mode=auto ! video/x-av1,framerate=60/1 "
        "! av1parse ! video/x-av1,stream-format=obu-stream,alignment=frame ",
    },
    // encoder::qsv
    {
        // codec::h264
        "qsvh264enc name=videncoder  bitrate={} rate-control=cbr "
        "target-usage=1 ! video/x-h264,framerate=60/1  ! h264parse "
        "config-interval=1 ",
        // codec::h265
        "qsvh265enc name=videncoder bitrate={} rate-control=cbr "
        "target-usage=1 ! video/x-h265,framerate=60/1  ! h265parse "
        "config-interval=1 ",
        // codec::av1
        "qsvav1enc name=videncoder bitrate={} rate-control=cbr "
        "target-usage=1 gop-size=120 ! video/x-av1,framerate=60/1 ! av1parse "
        "! video/x-av1,stream-format=obu-stream,alignment=frame ",
    },
    // encoder::nvenc
    {
        // codec::h264
        "nvh264enc name=videncoder bitrate={} rc-mode=cbr-hq "
        "preset=low-latency-hq ! h264parse config-interval=1 ",
        // codec::h265
        "nvh265enc name=videncoder bitrate={} rc-mode=cbr-hq "
        "preset=low-latency-hq ! h265parse config-interval=1 ",
        // codec::av1
        "nvav1enc name=videncoder bitrate={} rc-mode=cbr preset=low-latency-hq "
        "! av1parse ! video/x-av1,stream-format=obu-stream,alignment=frame ",
    },
    // encoder::software
    {
        // codec::h264
        "x264enc name=videncoder bitrate={} "
        "speed-preset=fast tune=zerolatency ! h264parse config-interval=1 ",
        // codec::h265
        "x265enc name=videncoder bitrate={} "
        "speed-preset=fast tune=zerolatency ! h265parse config-interval=1 ",
        // codec::av1
        "rav1enc name=videncoder bitrate={} speed-preset=8 tile-cols=2 "
        "tile-rows=2 ! av1parse "
        "! video/x-av1,stream-format=obu-stream,alignment=frame ",
    },
};
}  // namespace

void encode::pipeline_build_video_encoder()
{
  // Map encoder enum to a row, with unknown encoders falling back to software
  // (matches the old pipeline_build_video_encoder default branch).
  int enc_index = 0;
  switch (encode_c.selected_encoder) {
    case encoder::amd:
      enc_index = 0;
      break;
    case encoder::qsv:
      enc_index = 1;
      break;
    case encoder::nvenc:
      enc_index = 2;
      break;
    default:
      enc_index = 3;  // software
      break;
  }

  // Map codec enum to a column, with the 'default' codec falling back to h264
  // (matches the old per-vendor dispatcher default branch).
  int codec_index = 0;
  switch (encode_c.selected_codec) {
    case codec::h265:
      codec_index = 1;
      break;
    case codec::av1:
      codec_index = 2;
      break;
    default:
      codec_index = 0;  // h264
      break;
  }

  const std::string_view tmpl = kEncoderTemplates[enc_index][codec_index];
  const int bitrate_value = encode_c.bitrate.load(std::memory_order_relaxed);
  this->pipeline_str +=
      std::vformat(tmpl, std::make_format_args(bitrate_value));
}

void encode::pipeline_build_audio_payloader()
{
  this->pipeline_str += "! queue silent=true ! tsmux. ";
}

void encode::pipeline_build_video_payloader()
{
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
  std::lock_guard<std::mutex> guard(this->pipeline_mutex);
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
    return;
  }

  this->video_encoder =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "videncoder");
  this->video_sink =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "video_sink");
  this->audio_sink =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "audio_sink");

  this->bus = gst_element_get_bus(this->datasrc_pipeline);
  this->pipeline_cleaned_up.store(false, std::memory_order_release);
}

void encode::play_pipeline()
{
  encoder_running = true;
  const std::chrono::milliseconds duration(1);
  while (run_flag && run_flag->load() && encoder_running.load()) {
    GstBus* local_bus = nullptr;
    {
      std::lock_guard<std::mutex> guard(this->pipeline_mutex);
      if (this->bus != nullptr) {
        local_bus = this->bus;
        gst_object_ref(local_bus);
      }
    }
    if (local_bus == nullptr) {
      break;
    }
    GstMessage* msg = gst_bus_timed_pop(local_bus, GST_MSECOND);
    gst_object_unref(local_bus);
    if (msg != nullptr) {
      this->handle_gstreamer_message(msg);
      gst_message_unref(msg);
    } else {
      std::this_thread::sleep_for(duration);
    }
  }
}

void encode::run_encode_thread()
{
  this->build_pipeline();
  this->parse_pipeline();
  if (this->datasrc_pipeline == nullptr) {
    log("Refusing to start: pipeline failed to parse.\n");
    return;
  }
  log("Playing pipeline.\n");
  if (this->run_flag) {
    *this->run_flag = true;
  }
  gst_element_set_state(this->datasrc_pipeline, GST_STATE_PLAYING);
  threads.emplace_back([this] { play_pipeline(); });
}

void encode::stop_encode_thread()
{
  if (this->run_flag) {
    *this->run_flag = false;
  }
  this->encoder_running = false;

  for (auto& t : this->threads) {
    if (t.joinable()) {
      t.join();
    }
  }
  this->threads.clear();

  this->clear_pipeline_state();
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

auto encode::pull_from_sink(GstElement* encode::* sink_field) -> buffer_data
{
  GstElement* sink = nullptr;
  {
    std::lock_guard<std::mutex> guard(this->pipeline_mutex);
    sink = this->*sink_field;
    if (sink != nullptr) {
      gst_object_ref(sink);
    }
  }
  if (sink == nullptr) {
    return buffer_data {};
  }

  GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
  gst_object_unref(sink);

  if (sample == nullptr) {
    return buffer_data {};
  }
  GstBuffer* buffer = gst_sample_get_buffer(sample);
  if (buffer == nullptr) {
    gst_sample_unref(sample);
    return buffer_data {};
  }

  GstMapInfo info;
  if (gst_buffer_map(buffer, &info, GST_MAP_READ) == 0) {
    gst_sample_unref(sample);
    return buffer_data {};
  }

  buffer_data result;
  result.buf_size = info.size;
  if (info.data != nullptr && info.size > 0) {
    result.buf_data.assign(info.data, info.data + info.size);
  }
  gst_buffer_unmap(buffer, &info);
  gst_sample_unref(sample);
  return result;
}

auto encode::pull_video_buffer() -> buffer_data
{
  return pull_from_sink(&encode::video_sink);
}

auto encode::pull_audio_buffer() -> buffer_data
{
  return pull_from_sink(&encode::audio_sink);
}

void encode::set_encode_bitrate(int new_bitrate)
{
  if (new_bitrate <= 0) {
    return;
  }
  std::lock_guard<std::mutex> guard(this->pipeline_mutex);
  if (this->video_encoder == nullptr) {
    return;
  }
  g_object_set(G_OBJECT(this->video_encoder), "bitrate", new_bitrate, nullptr);
}

void encode::log(const std::string& msg) const
{
  if (log_func) {
    log_func(msg);
  }
}