#include <condition_variable>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <gst/gst.h>
#include <gst/gstdevice.h>

#include "stats.h"
#include "encode.h"
#include "transport.h"
#include "lib.h"
#include "ndi_input.h"
#include "ui.h"

library app;
std::unique_ptr<transport> transporter;
user_interface ui;
encode* ptr_encoder;

static auto encode_log(const std::string& msg)
{
  ui.encode_log_append(msg);
}

static auto transport_log(const std::string& msg)
{
  ui.transport_log_append(msg);
}

ndi_input ndi(app.input_config, &encode_log);

static void refresh_ndi_devices()
{
  ui.clear_ndi_choices();
  ui.add_ndi_choices(ndi.refresh_devices());
}


static auto rist_log_cb(void* arg,
                        enum rist_log_level log_level,
                        const char* msg) -> int
{
  ui.transport_log_append(msg);
  return 1;
}

static auto rist_stats_cb(const rist_stats& stats)
{
  if (ptr_encoder == nullptr) {
    return;
  }
  if (stats::got_rist_statistics(stats, &app.stats, app.encode_config, ui)) {
    ptr_encoder->set_encode_bitrate(app.stats.current_bitrate);
  }
}

static void run_loop()
{
  app.is_running = true;
  encode encoder(
      app.input_config, app.encode_config, &app.is_running, &encode_log);

  ptr_encoder = &encoder;

  encoder.run_encode_thread();

  while (app.is_running) {
    auto vidbuf = encoder.pull_video_buffer();
    if (vidbuf.buf_size > 0) {
      transporter->send_buffer(vidbuf, 0);
    }
  }
}

static void run()
{
  app.threads.emplace_back(run_loop);
}

static void stop()
{
  app.is_running = false;
}

static void run_transport()
{
  transporter = std::make_unique<transport>();
  transporter->set_log_callback(&rist_log_cb);
  transporter->set_statistics_callback(&rist_stats_cb);
  transporter->setup_rist_sender(app.output_config);
}

static void preview_input()
{
  switch (app.input_config.selected_input_mode) {
    case input_mode::sdp: {
      break;
    }

    case input_mode::ndi: {
      ndi.preview();
      break;
    }

    case input_mode::none: {
      break;
    }
    case input_mode::mpegts:
      break;
    case input_mode::test: {
      auto preview_thread = std::thread(
          [&]
          {
            std::string pipeline_string = std::format(
                "videotestsrc pattern=smpte is-live=true ! video/x-raw,width={},height={},framerate={}/1 ! videoconvert ! autovideosink",
                app.encode_config.width,
                app.encode_config.height,
                app.encode_config.framerate);
            auto *pipeline = gst_parse_launch(pipeline_string.c_str(), nullptr);
            auto *bus = gst_element_get_bus(pipeline);

            gst_element_set_state(pipeline, GST_STATE_PLAYING);
            auto *msg = gst_bus_timed_pop_filtered(
                bus,
                GST_CLOCK_TIME_NONE,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            /* Parse message */
            if (msg != nullptr) {
              GError* err = nullptr;
              gchar* debug_info = nullptr;

              switch (GST_MESSAGE_TYPE(msg)) {
                case GST_MESSAGE_ERROR:
                  encode_log(std::format("Error in test preview: {}",
                                         GST_OBJECT_NAME(msg->src)));
                  gst_message_parse_error(msg, &err, &debug_info);
                  encode_log(std::format("Error from element {}: {}\n",
                                         GST_OBJECT_NAME(msg->src),
                                         err->message));
                  encode_log(std::format("Debugging information: {}\n",
                                         (debug_info != nullptr) ? debug_info : "none"));
                  g_clear_error(&err);
                  g_free(debug_info);
                  break;
                case GST_MESSAGE_EOS:
                  encode_log("Test preview: End-Of-Stream reached.\n");
                  break;
                default:
                  /* We should not reach here because we only asked for ERRORs and
                  EOS */
                  break;
              }
              gst_message_unref(msg);
            }

            auto* loop = g_main_loop_new(nullptr, FALSE);
            g_main_loop_run(loop); // This function returns when the main loop is stopped.

            /* Free resources */
            gst_object_unref(bus);
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
          });
      preview_thread.detach();  // Detach thread before returning
      break;
    }
  }
}

auto main(int argc, char** argv) -> int
{
  gst_init(&argc, &argv);

  ndi.run_device_monitor();
  ui.init_ui();
  ui.init_ui_callbacks(&(app.input_config),
                       &(app.encode_config),
                       &(app.output_config),
                       &run,
                       &stop,
                       &refresh_ndi_devices,
                       &run_transport,
                       &preview_input);
  ui.show(argc, argv);
  const int result = ui.run_ui();
  return result;
}
