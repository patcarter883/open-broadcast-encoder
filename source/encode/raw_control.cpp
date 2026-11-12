// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

#include "encode/raw_control.h"

namespace
{
// The reader thread re-checks its stop flag at this cadence instead of parking
// in a blocking accept, so stop() returns without closing a descriptor out from
// under a syscall.
constexpr int kPollTimeoutMs = 200;

// A client that cannot take one short line in half a second is gone; waiting
// longer would stall the caller that owns this transition (the encode thread).
constexpr int kSendTimeoutMs = 500;

constexpr std::string_view kStartLine = "start\n";
constexpr std::string_view kStopLine = "stop\n";
}  // namespace

namespace raw_local
{
auto control_port(std::uint16_t frame_port) -> std::uint16_t
{
  if (frame_port == 0 || frame_port == 65535)
  {
    return 0;
  }
  return static_cast<std::uint16_t>(frame_port + 1);
}
}  // namespace raw_local

raw_control_server::raw_control_server(
    std::uint16_t port,
    std::function<void(const std::string&)> log_func)
    : port_ {port}
    , log_func_ {std::move(log_func)}
{
}

raw_control_server::~raw_control_server()
{
  this->stop();
}

void raw_control_server::start()
{
  if (this->worker.joinable())
  {
    return;
  }

  obc_socket::start_up();
  const obc_socket::handle fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd == obc_socket::invalid_handle)
  {
    this->log(std::format("[control] socket() failed: {}\n",
                          obc_socket::last_error()));
    return;
  }

  int on = 1;
  ::setsockopt(fd,
               SOL_SOCKET,
               SO_REUSEADDR,
               reinterpret_cast<const char*>(&on),
               sizeof(on));

  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(this->port_);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
  {
    // Not fatal: without the channel the plugin keeps its own Tools toggle, so
    // the operator loses convenience, not the ingest.
    this->log(std::format("[control] bind to port {} failed: {}; the OBS "
                          "plugin has to be started by hand\n",
                          this->port_,
                          obc_socket::last_error()));
    obc_socket::close_handle(fd);
    return;
  }
  if (::listen(fd, 1) != 0)
  {
    this->log(std::format("[control] listen failed: {}\n",
                          obc_socket::last_error()));
    obc_socket::close_handle(fd);
    return;
  }

  sockaddr_in actual {};
#if defined(_WIN32)
  int actual_len = sizeof(actual);
#else
  socklen_t actual_len = sizeof(actual);
#endif
  if (::getsockname(fd,
                    reinterpret_cast<sockaddr*>(&actual),
                    &actual_len) == 0)
  {
    this->bound_port.store(ntohs(actual.sin_port));
  }
  else
  {
    this->bound_port.store(this->port_);
  }

  this->listen_fd.store(fd);
  this->stop_requested.store(false);
  this->worker = std::thread([this] { this->run(); });
  this->log(std::format("[control] the OBS plugin is told when to run on "
                        "port {}\n",
                        this->bound_port.load()));
}

void raw_control_server::set_streaming(bool streaming_now)
{
  bool delivered = false;
  {
    const std::lock_guard<std::mutex> guard(this->client_mutex);
    this->streaming = streaming_now;
    delivered = this->client_fd != obc_socket::invalid_handle;
    this->write_state(streaming_now);
  }
  if (delivered)
  {
    this->log(std::format("[control] told the OBS plugin to {}\n",
                          streaming_now ? "start" : "stop"));
  }
}

void raw_control_server::stop()
{
  this->stop_requested.store(true);
  if (this->worker.joinable())
  {
    this->worker.join();
  }

  const obc_socket::handle fd =
      this->listen_fd.exchange(obc_socket::invalid_handle);
  if (fd != obc_socket::invalid_handle)
  {
    obc_socket::close_handle(fd);
  }

  const std::lock_guard<std::mutex> guard(this->client_mutex);
  if (this->client_fd != obc_socket::invalid_handle)
  {
    if (this->streaming)
    {
      // The listener is going away, so frames would land nowhere. Say so
      // before the descriptor goes, rather than leaving OBS sending.
      this->write_state(false);
    }
    obc_socket::close_handle(this->client_fd);
    this->client_fd = obc_socket::invalid_handle;
  }
}

auto raw_control_server::port() const -> std::uint16_t
{
  return this->bound_port.load();
}

void raw_control_server::run()
{
  while (!this->stop_requested.load())
  {
    const obc_socket::handle listener = this->listen_fd.load();
    if (listener == obc_socket::invalid_handle)
    {
      return;
    }

    obc_socket::poll_descriptor pfd {};
    pfd.fd = listener;
    pfd.events = POLLIN;
    const int ready = obc_socket::poll(&pfd, 1, kPollTimeoutMs);
    if (ready <= 0)
    {
      continue;  // timeout, or a signal: the stop flag decides
    }

    const obc_socket::handle client = ::accept(listener, nullptr, nullptr);
    if (client == obc_socket::invalid_handle)
    {
      if (obc_socket::interrupted())
      {
        continue;
      }
      if (this->stop_requested.load())
      {
        return;
      }
      continue;
    }

    obc_socket::set_send_timeout_ms(client, kSendTimeoutMs);

    {
      const std::lock_guard<std::mutex> guard(this->client_mutex);
      if (this->client_fd != obc_socket::invalid_handle)
      {
        // One plugin per encoder: a second connection is a restarted OBS, so
        // the old descriptor is dead even if it has not said so.
        obc_socket::close_handle(this->client_fd);
      }
      this->client_fd = client;
      this->write_state(this->streaming);
    }
    this->log("[control] the OBS plugin connected\n");
  }
}

// Caller holds client_mutex.
void raw_control_server::write_state(bool streaming_now)
{
  if (this->client_fd == obc_socket::invalid_handle)
  {
    return;
  }

  const std::string_view line = streaming_now ? kStartLine : kStopLine;
  const auto sent = ::send(this->client_fd,
                           line.data(),
                           static_cast<int>(line.size()),
                           obc_socket::no_signal);
  if (sent == static_cast<decltype(sent)>(line.size()))
  {
    return;
  }

  // The peer is gone. The plugin re-dials, and its next accept is told the
  // current state, so nothing is lost by dropping this descriptor here.
  obc_socket::close_handle(this->client_fd);
  this->client_fd = obc_socket::invalid_handle;
}

void raw_control_server::log(const std::string& msg) const
{
  if (this->log_func_)
  {
    this->log_func_(msg);
  }
}
