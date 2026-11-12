// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The control channel that starts and stops the OBS plugin's output with the
// encoder. Two things have to hold, and neither is visible from a pipeline:
// the port pairing both sides derive the same way, and the state a client is
// told (a plugin that connects after the encoder has already started must be
// told to start, not wait for the next transition).
//
// Compiled against raw_control.cpp with no GStreamer pipeline, the way
// raw_local_port_test pins the ingest's port decision.

#include <cstddef>
#include <cstdint>
#include <string>

#include "encode/raw_control.h"

#include <catch2/catch_test_macros.hpp>

#include "encode/socket_compat.h"

namespace
{
// The plugin dials the configured target's port + 1; this is the same rule on
// the encoder side, so the pair is pinned here once.
TEST_CASE("the control port is the frame port plus one", "[raw_control]")
{
  REQUIRE(raw_local::control_port(9300) == 9301);
  REQUIRE(raw_local::control_port(5000) == 5001);
  REQUIRE(raw_local::control_port(65534) == 65535);
}

TEST_CASE("a port with no room above it has no control channel",
          "[raw_control]")
{
  REQUIRE(raw_local::control_port(65535) == 0);
  REQUIRE(raw_local::control_port(0) == 0);
}

auto connect_local(std::uint16_t port) -> obc_socket::handle
{
  obc_socket::start_up();
  const obc_socket::handle fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd == obc_socket::invalid_handle) {
    return fd;
  }

  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    obc_socket::close_handle(fd);
    return obc_socket::invalid_handle;
  }
  return fd;
}

// One line, or empty when none arrived before the deadline. The server tells a
// freshly accepted client the current state, so this is what a late plugin
// sees.
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

TEST_CASE("a client is told the state it connected into", "[raw_control]")
{
  raw_control_server server(0, {});
  REQUIRE(server.port() == 0);
  server.start();
  REQUIRE(server.port() != 0);

  // Start before the client connects: the accept path has to report the
  // current state, which is the whole reason a restarted OBS resumes.
  server.set_streaming(true);

  const obc_socket::handle client = connect_local(server.port());
  REQUIRE(client != obc_socket::invalid_handle);
  REQUIRE(read_line(client, 2000) == "start\n");

  // Then the far edge, on the same connection.
  server.set_streaming(false);
  REQUIRE(read_line(client, 2000) == "stop\n");

  obc_socket::close_handle(client);
  server.stop();
}

TEST_CASE("a second connection replaces the first", "[raw_control]")
{
  raw_control_server server(0, {});
  server.start();
  REQUIRE(server.port() != 0);

  const obc_socket::handle first = connect_local(server.port());
  REQUIRE(first != obc_socket::invalid_handle);
  // Consume the state line so the second connect is the only pending event.
  REQUIRE(read_line(first, 2000) == "stop\n");

  const obc_socket::handle second = connect_local(server.port());
  REQUIRE(second != obc_socket::invalid_handle);
  REQUIRE(read_line(second, 2000) == "stop\n");

  // The replacement is the live one: a transition reaches it, and it alone.
  server.set_streaming(true);
  REQUIRE(read_line(second, 2000) == "start\n");

  obc_socket::close_handle(first);
  obc_socket::close_handle(second);
  server.stop();
}
}  // namespace
