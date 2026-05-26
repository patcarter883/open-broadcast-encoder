#include <arpa/inet.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <gst/gst.h>
#include <gst/video/video.h>

// #include "common.h"
#include "encode.h"
#include "lib.h"
#include "ndi_input.h"
#include "stats.h"
#include "transport.h"
#include "ui.h"

app_context ctx;

static auto encode_log(const std::string& msg)
{
  if (ctx.ui != nullptr) {
    ctx.ui->encode_log_append(msg);
  }
}

static auto transport_log(const std::string& msg)
{
  if (ctx.ui != nullptr) {
    ctx.ui->transport_log_append(msg);
  }
}

static void refresh_ndi_devices()
{
  if (ctx.ui != nullptr && ctx.ndi != nullptr) {
    ctx.ui->clear_ndi_choices();
    ctx.ui->add_ndi_choices(ctx.ndi->refresh_devices());
  }
}

static auto rist_log_cb(void* arg,
                        enum rist_log_level log_level,
                        const char* msg) -> int
{
  if (ctx.ui != nullptr) {
    ctx.ui->transport_log_append(msg);
  }
  return 1;
}

static auto rist_stats_cb(const rist_stats& stats)
{
  if (stats::got_rist_statistics(
          stats, &ctx.lib.stats, ctx.lib.encode_cfg, *ctx.ui))
  {
    if (ctx.lib.encoder_ptr != nullptr) {
      ctx.lib.encoder_ptr->set_encode_bitrate(ctx.lib.stats.current_bitrate);
    }
  }
}

static void rist_oob_cb(const uint8_t* data, size_t size)
{
  if (size != sizeof(wan_telemetry)) {
    return;
  }
  wan_telemetry tel;
  std::memcpy(&tel, data, sizeof(tel));
  const uint32_t rtt_ms = ntohl(tel.worst_case_rtt);

  ctx.lib.stats.wan_quality = tel.link_quality;
  ctx.lib.stats.wan_rtt = rtt_ms;

  if (ctx.ui != nullptr) {
    ctx.ui->lock();
    ctx.ui->wan_quality_output->value(std::to_string(tel.link_quality).c_str());
    ctx.ui->wan_rtt_output->value(std::to_string(rtt_ms).c_str());
    ctx.ui->unlock();
  }

  if (ctx.lib.encode_cfg.scaling_source == bitrate_source::remote_oob) {
    if (stats::scale_encoder_bitrate(
            static_cast<double>(tel.link_quality),
            &ctx.lib.stats,
            ctx.lib.encode_cfg))
    {
      if (ctx.lib.encoder_ptr != nullptr) {
        ctx.lib.encoder_ptr->set_encode_bitrate(ctx.lib.stats.current_bitrate);
      }
    }
  }
}

static void run_loop()
{
  ctx.lib.is_running = true;
  ctx.lib.stats.current_bitrate = ctx.lib.encode_cfg.bitrate;
  ctx.lib.stats.previous_quality = 0.0;
  ctx.lib.encoder_ptr = std::make_shared<encode>(
      ctx.lib.input_cfg, ctx.lib.encode_cfg, ctx.lib.run_flag, &encode_log);

  ctx.lib.encoder_ptr->run_encode_thread();

  ctx.transporter->set_statistics_callback(&rist_stats_cb);

  while (ctx.lib.is_running) {
    auto vidbuf = ctx.lib.encoder_ptr->pull_video_buffer();
    if (!vidbuf.buf_data.empty()) {
      ctx.transporter->send_buffer(vidbuf.buf_data, 0);
    }
  }
}

static void run()
{
  ctx.lib.threads.emplace_back(run_loop);
}

static void stop()
{
  ctx.lib.is_running = false;
  if (ctx.lib.encoder_ptr != nullptr) {
    ctx.transporter->set_statistics_callback(nullptr);
    ctx.transporter->set_oob_callback(nullptr);
    ctx.lib.encoder_ptr->stop_encode_thread();
    ctx.lib.encoder_ptr = nullptr;
  }
}

static void run_transport()
{
  ctx.transporter = std::make_unique<transport>();
  ctx.transporter->set_log_callback(&rist_log_cb);
  ctx.transporter->set_statistics_callback(&rist_stats_cb);
  ctx.transporter->set_oob_callback(&rist_oob_cb);
  ctx.transporter->setup_rist_sender(ctx.lib.output_cfg);
}

static void scaling_source_changed()
{
  ctx.lib.stats.previous_quality = 0.0;
  ctx.lib.stats.current_bitrate = ctx.lib.encode_cfg.bitrate;
}

static void run_preview_pipeline(std::string pipeline_str)
{
  auto* pipeline = gst_parse_launch(pipeline_str.c_str(), nullptr);
  if (!pipeline) {
    return;
  }

  auto* bus = gst_element_get_bus(pipeline);
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  auto* msg = gst_bus_timed_pop_filtered(
      bus,
      GST_CLOCK_TIME_NONE,
      static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

  if (msg != nullptr) {
    switch (GST_MESSAGE_TYPE(msg)) {
      case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* debug_info = nullptr;
        gst_message_parse_error(msg, &err, &debug_info);
        encode_log(std::format("Preview error: {}\n", err->message));
        g_clear_error(&err);
        g_free(debug_info);
        break;
      }
      case GST_MESSAGE_EOS:
        encode_log("Preview ended.\n");
        break;
      default:
        break;
    }
    gst_message_unref(msg);
  }

  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
}

static void preview_input()
{
  switch (ctx.lib.input_cfg.selected_input_mode) {
    case input_mode::testsrc: {
      run_preview_pipeline(
          "audiotestsrc is-live=true ! audioconvert ! "
          "videotestsrc pattern=smptebars ! videoconvert ! "
          "autovideosink audiotestsrc is-live=true ! autoaudiosink");
      break;
    }

    case input_mode::mpegts: {
      auto port = ctx.lib.input_cfg.selected_input;
      run_preview_pipeline(
          std::format("udpsrc port={} ! tsdemux name=d ! d.video ! queue ! "
                      "videoconvert ! autovideosink d.audio ! queue ! "
                      "audioconvert ! autoaudiosink",
                      port));
      break;
    }

    case input_mode::sdp: {
      auto* const sdp =
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
      auto tmp_path = std::filesystem::temp_directory_path() / "preview.sdp";
      std::ofstream(tmp_path) << sdp;
      run_preview_pipeline(std::format(
          "sdpsrc uri=\"file://{}\" ! rtpvrawdepay ! videoconvert ! "
          "autovideosink",
          tmp_path.string()));
      std::filesystem::remove(tmp_path);
      break;
    }

    case input_mode::ndi: {
      if (ctx.ndi != nullptr) {
        ctx.ndi->preview();
      }
      break;
    }

    case input_mode::none: {
      break;
    }
  }
}

auto main(int argc, char** argv) -> int
{
  gst_init(&argc, &argv);

  user_interface ui;
  ctx.ui = &ui;
  ctx.ui->init_ui();
  ctx.ndi = std::make_unique<ndi_input>(ctx.lib.input_cfg, &encode_log);
  ctx.ndi->run_device_monitor();
  ctx.ui->init_ui_callbacks(&(ctx.lib.input_cfg),
                            &(ctx.lib.encode_cfg),
                            &(ctx.lib.output_cfg),
                            &run,
                            &stop,
                            &refresh_ndi_devices,
                            &run_transport,
                            &preview_input,
                            &scaling_source_changed);
  ctx.ui->show(argc, argv);
  const int result = ctx.ui->run_ui();
  return result;
}
