// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <atomic>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include <gst/gst.h>
#include <gst/gstdevice.h>
#include <gst/gstdevicemonitor.h>

#include "lib/lib.h"

using log_func_ptr = void (*)(const std::string& msg);

struct ndi_input
{
  explicit ndi_input(const input_config& input_config, log_func_ptr log_func);
  ~ndi_input();
  ndi_input(const ndi_input&) = delete;
  ndi_input& operator=(const ndi_input&) = delete;
  ndi_input(ndi_input&&) = delete;
  ndi_input& operator=(ndi_input&&) = delete;

  log_func_ptr log_func = nullptr;
  const input_config& input_c;
  GstDeviceMonitor* device_monitor = nullptr;

  std::thread device_monitor_thread;
  std::thread preview_thread;

  std::atomic<bool> run_monitor {false};
  std::atomic<bool> preview_running {false};

  auto run_device_monitor() -> void;
  auto stop_device_monitor() -> void;
  auto refresh_devices() const -> std::vector<std::string>;
  auto preview() -> void;
  auto stop_preview() -> void;

private:
  void log(const std::string& msg) const;
};
