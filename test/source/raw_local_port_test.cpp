// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// Which port the OBS raw ingest binds, and what happens when the listen-port
// field does not hold a port. That field is shared with the MPEG-TS ingest and
// once held an NDI device name ("BLUE (OBS PGM)"), so a non-numeric value
// reaching the reader is a case that happens in the field, not a theoretical
// one: it must fall back to the plugin's default rather than fail the start.
//
// Compiled against raw_local.cpp's resolver with no GStreamer pipeline, the way
// capture_source_test pins the capture ingest's addressing decision.

#include <string>

#include "encode/raw_local_port.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a configured port is used when it is a port", "[raw_local]")
{
  REQUIRE(raw_local::resolve_port("9300") == 9300);
  REQUIRE(raw_local::resolve_port("1") == 1);
  REQUIRE(raw_local::resolve_port("65535") == 65535);
  REQUIRE(raw_local::resolve_port("5000") == 5000);
}

TEST_CASE("the default is the OBS raw output's own target", "[raw_local]")
{
  // obs-raw-output/src/raw-output.c: OBC_DEFAULT_TARGET "127.0.0.1:9300".
  REQUIRE(raw_local::k_default_port == 9300);
  REQUIRE(raw_local::resolve_port("") == raw_local::k_default_port);
}

TEST_CASE("anything that is not a usable port falls back to the default",
          "[raw_local]")
{
  // The historical contents of this field: an NDI device name.
  REQUIRE(raw_local::resolve_port("BLUE (OBS PGM)")
          == raw_local::k_default_port);
  // Out of range both ways.
  REQUIRE(raw_local::resolve_port("0") == raw_local::k_default_port);
  REQUIRE(raw_local::resolve_port("65536") == raw_local::k_default_port);
  REQUIRE(raw_local::resolve_port("-1") == raw_local::k_default_port);
}

TEST_CASE("a trailing suffix is read as far as it parses", "[raw_local]")
{
  // std::stoi semantics, unchanged from the code this replaced: the numeric
  // prefix wins. Pinned so the behaviour is a decision, not an accident.
  REQUIRE(raw_local::resolve_port("5000abc") == 5000);
}
