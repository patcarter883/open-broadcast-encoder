#include <condition_variable>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <gst/gst.h>
#include <gst/gstdevice.h>

// #include "common.h"
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
