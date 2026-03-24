#include "ndi_input/ndi_input.h"
#include <format>
#include <thread>
#include <chrono>
#include <gst/gst.h>
#include <gst/gstbus.h>
#include <gst/gstcaps.h>
#include <gst/gstdevice.h>
#include <gst/gstdevicemonitor.h>

ndi_input::ndi_input(const input_config& input_config, const log_func_ptr log_func): 
log_func {log_func},
input_c {input_config}
{}

auto ndi_input::run_device_monitor() -> void
{
  run_monitor = true;
  device_monitor = gst_device_monitor_new();
  device_monitor_thread = std::thread(
      [&]
      {
        auto* caps = gst_caps_new_empty_simple("application/x-ndi");
        gst_device_monitor_add_filter(device_monitor, "Video/Source", caps);
        gst_caps_unref(caps);

        gst_device_monitor_start(device_monitor);


        std::chrono::seconds duration(1);
        while (run_monitor) {
          std::this_thread::yield();
          std::this_thread::sleep_for(duration);
        }
      });
}

auto ndi_input::refresh_devices() const -> std::vector<std::string>
{
  auto device_names = std::vector<std::string>();
  GList* devices = gst_device_monitor_get_devices(device_monitor);

  for (GList* list_item = devices; list_item != nullptr; list_item = list_item->next) {
    auto* device = static_cast<GstDevice*>(list_item->data);

    char* device_name = gst_device_get_display_name(device);
    if (device_name != nullptr) {
      device_names.emplace_back(device_name);
      g_free(device_name);
    }
    gst_object_unref(device);
  }
  g_list_free(devices);
  return device_names;
}

auto ndi_input::stop_device_monitor() -> void
{
  run_monitor = false;
  if (device_monitor_thread.joinable()) {
    device_monitor_thread.join();
  }
  if (device_monitor != nullptr) {
    gst_device_monitor_stop(device_monitor);
    gst_object_unref(device_monitor);
    device_monitor = nullptr;
  }
}

auto ndi_input::preview() -> void
{
  preview_thread = std::thread(
      [&]
      {
        std::string pipeline_string = std::format(
            "ndisrc ndi-name=\"{}\" do-timestamp=true ! ndisrcdemux name=demux "
            "  "
            "demux.video ! queue ! videoconvert ! autovideosink  demux.audio ! "
            "queue "
            "! audioconvert ! autoaudiosink",
            input_c.selected_input);
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
              log(pipeline_string);
              gst_message_parse_error(msg, &err, &debug_info);
              log(std::format("Error received from element {}: {}\n",
                                         GST_OBJECT_NAME(msg->src),
                                         err->message));
              log(std::format("Debugging information: {}\n",
                                         (debug_info != nullptr) ? debug_info : "none"));
              g_clear_error(&err);
              g_free(debug_info);
              break;
            case GST_MESSAGE_EOS:
              log("End-Of-Stream reached.\n");
              break;
            default:
              /* We should not reach here because we only asked for ERRORs and
      EOS */ log("Unexpected message received.\n");
              break;
          }
          gst_message_unref(msg);
        }

        auto* loop = g_main_loop_new(nullptr, FALSE);
        g_main_loop_run(loop);

        /* Free resources */
        g_main_loop_unref(loop);
        gst_object_unref(bus);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
      });
}

void ndi_input::log(const std::string& msg) const
{
  if (log_func != nullptr) {
    log_func(msg);
  }
}