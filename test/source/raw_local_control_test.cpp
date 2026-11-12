// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The encoder half of the start/stop handshake, driven through the real
// raw_local_input rather than a stand-in: the reader opens the frame listener,
// the control channel appears beside it, and the plugin is told to start. Then
// the reverse at teardown, because a stopped preview that leaves OBS sending
// into nothing is the failure this feature exists to remove.
//
// Kept out of the unit test's reach on purpose: raw_control_test pins the
// protocol, this pins that raw_local_input actually opens and closes it.

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include "encode/raw_local.h"

#include <catch2/catch_test_macros.hpp>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include "encode/raw_control.h"
#include "encode/socket_compat.h"

namespace
{
// Fixed rather than ephemeral: raw_local_input takes the frame port and derives
// the control port from it, so a test that let the OS choose could not say
// where to look. Both are above the ephemeral range in practice.
constexpr std::uint16_t kFramePort = 19300;
constexpr std::uint16_t kControlPort = 19301;

struct pipeline_guard
{
  GstElement* pipeline = nullptr;

  ~pipeline_guard()
  {
    if (pipeline != nullptr) {
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
    }
  }
};

auto connect_control() -> obc_socket::handle
{
  obc_socket::start_up();
  const obc_socket::handle fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd == obc_socket::invalid_handle) {
    return fd;
  }

  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(kControlPort);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    obc_socket::close_handle(fd);
    return obc_socket::invalid_handle;
  }
  return fd;
}

// The control listener opens on the reader's thread, so the first connect can
// legitimately lose a race with it.
auto connect_control_when_ready() -> obc_socket::handle
{
  for (int attempt = 0; attempt < 60; ++attempt) {
    const obc_socket::handle fd = connect_control();
    if (fd != obc_socket::invalid_handle) {
      return fd;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return obc_socket::invalid_handle;
}

auto read_line(obc_socket::handle fd, int timeout_ms) -> std::string
{
  obc_socket::poll_descriptor pfd {};
  pfd.fd = fd;
  pfd.events = POLLIN;
  if (obc_socket::poll(&pfd, 1, timeout_ms) <= 0) {
    return {};
  }

  char buf[64] {};
  const auto got = ::recv(fd, buf, sizeof(buf) - 1, 0);
  if (got <= 0) {
    return {};
  }
  return std::string {buf, static_cast<std::size_t>(got)};
}

TEST_CASE("the raw ingest tells the plugin to start and stop with it",
          "[raw_local][control]")
{
  gst_init(nullptr, nullptr);

  pipeline_guard guard;
  guard.pipeline = gst_parse_launch(
      "appsrc name=rawvideo is-live=true format=time "
      "caps=video/x-raw,format=NV12,width=64,height=64,framerate=30/1 "
      "! fakesink async=false sync=false "
      "appsrc name=rawaudio is-live=true format=time "
      "caps=audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved "
      "! fakesink async=false sync=false",
      nullptr);
  REQUIRE(guard.pipeline != nullptr);

  auto* video = gst_bin_get_by_name(GST_BIN(guard.pipeline), "rawvideo");
  auto* audio = gst_bin_get_by_name(GST_BIN(guard.pipeline), "rawaudio");
  REQUIRE(video != nullptr);
  REQUIRE(audio != nullptr);

  std::string last_status;
  auto log_line = [](const std::string&) {};
  auto status_line = [&last_status](const std::string& msg)
  { last_status = msg; };

  {
    raw_local_input reader(
        video, audio, kFramePort, log_line, {}, status_line, push_mode::drop);
    reader.start();

    const obc_socket::handle client = connect_control_when_ready();
    REQUIRE(client != obc_socket::invalid_handle);

    // The reader is previewing from the plugin's point of view the moment its
    // listener is up, so a client that arrives late is told to start.
    REQUIRE(read_line(client, 3000) == "start\n");

    reader.stop();

    // Teardown is an explicit stop: the plugin's output must not be left
    // sending into a listener that has gone.
    REQUIRE(read_line(client, 3000) == "stop\n");

    obc_socket::close_handle(client);
  }

  gst_object_unref(video);
  gst_object_unref(audio);
}
}  // namespace
