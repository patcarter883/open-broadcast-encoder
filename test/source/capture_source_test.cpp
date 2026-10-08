// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The unicast addressing half of the JPEG XS capture ingest (MC4). Compiled
// directly against capture_source.cpp, which carries no GStreamer types, so the
// decision the live run depends on -- which interface the reader binds -- is
// pinned without a pipeline.

#include <string>

#include "encode/capture_source.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a listen port falls back rather than failing the start", "[capture]")
{
  // A usable value passes through, including the boundaries.
  REQUIRE(capture::parse_stream_port("5000") == 5000);
  REQUIRE(capture::parse_stream_port("1") == 1);
  REQUIRE(capture::parse_stream_port("65535") == 65535);

  // Everything the operator can type that is not a usable port falls back to
  // the node's default, rather than aborting the start (raw_local's rule).
  REQUIRE(capture::parse_stream_port("") == capture::k_default_port);
  REQUIRE(capture::parse_stream_port("0") == capture::k_default_port);
  REQUIRE(capture::parse_stream_port("65536") == capture::k_default_port);
  REQUIRE(capture::parse_stream_port("-1") == capture::k_default_port);
  REQUIRE(capture::parse_stream_port("udp://10.50.1.51:5000")
          == capture::k_default_port);
  REQUIRE(capture::parse_stream_port("abc") == capture::k_default_port);

  // An explicit fallback is honoured.
  REQUIRE(capture::parse_stream_port("", 9300) == 9300);
}

TEST_CASE("the bind address is the interface that routes to the camera",
          "[capture]")
{
  // Loopback is the one destination whose local address is fixed regardless of
  // this host's interfaces, so it is the deterministic case.
  REQUIRE(capture::local_address_for("127.0.0.1") == "127.0.0.1");

  // No destination, or the wildcard, means "bind the wildcard" -- the manual
  // fallback, expressed as an empty address.
  REQUIRE(capture::local_address_for("") == "");
  REQUIRE(capture::local_address_for("0.0.0.0") == "");

  // An address that cannot be parsed as IPv4 resolves to nothing, so the reader
  // binds the wildcard instead of failing.
  REQUIRE(capture::local_address_for("not-an-address") == "");
}
