// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <chrono>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include "ndi_input/ndi_input.h"

#include <gst/gst.h>
#include <gst/gstbus.h>
#include <gst/gstcaps.h>
#include <gst/gstdevice.h>
#include <gst/gstdevicemonitor.h>

ndi_input::ndi_input(const input_config& input_config,
                     const log_func_ptr log_func)
    : log_func {log_func}
    , input_c {input_config}
{
}

ndi_input::~ndi_input()
{
  this->stop_preview();
  this->stop_device_monitor();

  if (this->device_monitor_thread.joinable()) {
    this->device_monitor_thread.join();
  }
  if (this->preview_thread.joinable()) {
    this->preview_thread.join();
  }

  if (this->device_monitor != nullptr) {
    gst_device_monitor_stop(this->device_monitor);
    gst_object_unref(this->device_monitor);
    this->device_monitor = nullptr;
  }
}

auto ndi_input::run_device_monitor() -> void
{
  this->run_monitor = true;
  this->device_monitor = gst_device_monitor_new();
  this->device_monitor_thread = std::thread(
      [this]
      {
        // GLib main context is required so the NDI device provider can fire
        // timers and post device-added messages; without it the device list
        // stays empty even when NDI sources are on the network.
        GMainContext* glib_ctx = g_main_context_new();
        g_main_context_push_thread_default(glib_ctx);

        auto* caps = gst_caps_new_empty_simple("application/x-ndi");
        const guint filter_id = gst_device_monitor_add_filter(
            this->device_monitor, "Video/Source", caps);
        gst_caps_unref(caps);

        log(std::format(
            "NDI monitor: filter_id={} (0 means monitor already started or "
            "error)\n",
            filter_id));

        if (!gst_device_monitor_start(this->device_monitor)) {
          log("NDI monitor: gst_device_monitor_start() failed\n");
        } else {
          log("NDI monitor: started\n");
        }

        const std::chrono::milliseconds tick(100);
        while (this->run_monitor.load(std::memory_order_acquire)) {
          g_main_context_iteration(glib_ctx, FALSE);
          std::this_thread::sleep_for(tick);
        }

        g_main_context_pop_thread_default(glib_ctx);
        g_main_context_unref(glib_ctx);
      });
}

auto ndi_input::refresh_devices() const -> std::vector<std::string>
{
  std::vector<std::string> device_names;
  if (this->device_monitor == nullptr) {
    log("NDI refresh: device monitor is null\n");
    return device_names;
  }

  GList* devices = gst_device_monitor_get_devices(this->device_monitor);
  const guint n = g_list_length(devices);
  log(std::format("NDI refresh: {} device(s) found\n", n));

  for (GList* list_item = devices; list_item != nullptr;
       list_item = list_item->next)
  {
    auto* device = static_cast<GstDevice*>(list_item->data);

    gchar* device_name = gst_device_get_display_name(device);
    gchar* device_class = gst_device_get_device_class(device);
    GstCaps* device_caps = gst_device_get_caps(device);
    gchar* caps_str =
        (device_caps != nullptr) ? gst_caps_to_string(device_caps) : nullptr;

    log(std::format("  device: name='{}' class='{}' caps='{}'\n",
                    device_name != nullptr ? device_name : "(null)",
                    device_class != nullptr ? device_class : "(null)",
                    caps_str != nullptr ? caps_str : "(null)"));

    if (device_name != nullptr) {
      device_names.emplace_back(device_name);
    }
    g_free(device_name);
    g_free(device_class);
    if (device_caps != nullptr) {
      gst_caps_unref(device_caps);
    }
    g_free(caps_str);
    gst_object_unref(device);
  }
  g_list_free(devices);
  return device_names;
}

auto ndi_input::stop_device_monitor() -> void
{
  this->run_monitor = false;
}

auto ndi_input::stop_preview() -> void
{
  this->preview_running = false;
}

auto ndi_input::preview() -> void
{
  if (this->preview_thread.joinable()) {
    if (this->preview_running.load(std::memory_order_acquire)) {
      return;  // preview still running; leave it rather than blocking the UI
    }
    this->preview_thread.join();  // thread finished its cleanup; quick join
  }
  this->preview_running = true;
  this->preview_thread = std::thread(
      [this]
      {
        std::string pipeline_string = std::format(
            "ndisrc ndi-name=\"{}\" do-timestamp=true ! ndisrcdemux name=demux "
            "demux.video ! queue ! videoconvert ! autovideosink demux.audio ! "
            "queue ! audioconvert ! autoaudiosink",
            this->input_c.selected_input);
        auto* pipeline = gst_parse_launch(pipeline_string.c_str(), nullptr);
        if (pipeline == nullptr) {
          log("Failed to parse preview pipeline.\n");
          this->preview_running = false;
          return;
        }
        auto* bus = gst_element_get_bus(pipeline);

        gst_element_set_state(pipeline, GST_STATE_PLAYING);

        bool keep_going = true;
        while (keep_going
               && this->preview_running.load(std::memory_order_acquire))
        {
          auto* msg = gst_bus_timed_pop_filtered(
              bus,
              static_cast<GstClockTime>(100 * GST_MSECOND),
              static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
          if (msg == nullptr) {
            continue;
          }
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
              break;
          }
          gst_message_unref(msg);
          keep_going = false;
        }

        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(pipeline);
        this->preview_running = false;
      });
}

void ndi_input::log(const std::string& msg) const
{
  if (log_func != nullptr) {
    log_func(msg);
  }
}
