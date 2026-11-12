// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "encode/socket_compat.h"

// The channel that tells the OBS raw output plugin when to run.
//
// obs-raw-output's plugin dials this port and obeys one word per line. The
// encoder decides, so previewing or encoding starts the plugin's output and
// stopping either one stops it -- without this the operator had to hold a
// Tools-menu toggle in OBS in step with the encoder, and forgetting it looked
// exactly like a broken encoder.
//
// The port is the frame port + 1, derived on both sides from the target the
// plugin is configured with: host:9300 pairs with a control port of 9301.
//
// State, not events. A client that connects late -- OBS started after the
// encoder, or restarted mid-session -- is told the current state immediately on
// accept, and every transition is written to the connected client. A dropped
// connection is not read as "stop": the plugin re-dials, and the encoder keeps
// its own listener up meanwhile.
namespace raw_local
{
// The frame port pairs with the port above it. 0 when there is no room above
// it, which the caller reads as "no control channel on this port".
auto control_port(std::uint16_t frame_port) -> std::uint16_t;
}  // namespace raw_local

class raw_control_server
{
public:
  // `port` of 0 lets the OS choose, which is what the test uses; port() then
  // reports what was bound.
  raw_control_server(std::uint16_t port,
                     std::function<void(const std::string&)> log_func);
  ~raw_control_server();

  raw_control_server(const raw_control_server&) = delete;
  raw_control_server& operator=(const raw_control_server&) = delete;
  raw_control_server(raw_control_server&&) = delete;
  raw_control_server& operator=(raw_control_server&&) = delete;

  // Binds and listens synchronously, then accepts on its own thread.
  void start();
  // What a connected client is told, now and on its next connect.
  void set_streaming(bool streaming);
  void stop();

  auto port() const -> std::uint16_t;

private:
  void run();
  void write_state(bool streaming);

  std::uint16_t port_;
  std::function<void(const std::string&)> log_func_;

  std::thread worker;
  std::atomic<bool> stop_requested {false};
  std::atomic<obc_socket::handle> listen_fd {obc_socket::invalid_handle};
  std::atomic<std::uint16_t> bound_port {0};

  // Guards the client descriptor and the state a client is told, so a
  // transition and an accept cannot interleave into a stale line.
  std::mutex client_mutex;
  obc_socket::handle client_fd {obc_socket::invalid_handle};
  bool streaming {false};

  void log(const std::string& msg) const;
};
