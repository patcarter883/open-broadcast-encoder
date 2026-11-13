// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The RIST sender's declared bandwidth, pinned without libRIST: the figure
// decides whether ARQ is funded at all, and a ceiling under the payload leaves
// libRIST with no retransmission budget.

#include <optional>

#include "lib/rist_bandwidth.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a bridged session keeps the flat ceiling", "[rist][bandwidth]")
{
  // The encoder's hop is the LAN; the WAN budget is the bridge's own leg.
  CHECK(rist_bandwidth_kbps(4300, std::nullopt, true) == 20000);
  CHECK(rist_bandwidth_kbps(30000, std::nullopt, true) == 20000);
  CHECK(rist_bandwidth_kbps(30000, 5000, true) == 20000);
}

TEST_CASE("a direct session funds retransmission over its payload",
          "[rist][bandwidth]")
{
  // A 30 Mbit/s direct session under the flat 20000 states a -10000 kbps
  // retransmission budget, which is libRIST being told to send no retransmits.
  const int ceiling = rist_bandwidth_kbps(30000, std::nullopt, false);
  CHECK(ceiling == 30000 + rist_direct_headroom_kbps);
  CHECK(ceiling > 30000);
}

TEST_CASE("a measurement below the payload never cuts the payload",
          "[rist][bandwidth]")
{
  // A trickle measured on the uplink is not a reason to declare less than the
  // sender is asked to send: the result is clamped up to the payload.
  CHECK(rist_bandwidth_kbps(30000, std::optional<int> {25000}, false) == 30000);
  CHECK(rist_bandwidth_kbps(30000, std::optional<int> {1000}, false) == 30000);
}

TEST_CASE("a measurement sizes the sender only for its own destination",
          "[rist][bandwidth]")
{
  const std::string destination = "rist://198.51.100.9:5100";
  const auto applied = measured_upload_kbps_for(7400, destination, destination);
  REQUIRE(applied.has_value());
  CHECK(*applied == 7400);

  // A figure taken over the path to another address says nothing about this
  // one, so it must not size anything.
  CHECK_FALSE(
      measured_upload_kbps_for(7400, destination, "rist://203.0.113.4:6000")
          .has_value());
  // Nor does a figure with no record of where it came from, or none at all.
  CHECK_FALSE(
      measured_upload_kbps_for(7400, std::string {}, destination).has_value());
  CHECK_FALSE(
      measured_upload_kbps_for(0, destination, destination).has_value());
  CHECK_FALSE(
      measured_upload_kbps_for(-1, destination, destination).has_value());
}

TEST_CASE("a measurement bounds the ceiling when it is the smaller figure",
          "[rist][bandwidth]")
{
  // 32 Mbit/s measured, 30 Mbit/s payload: the measurement wins over the
  // headroom figure.
  CHECK(rist_bandwidth_kbps(30000, std::optional<int> {32000}, false) == 32000);
  // Plenty measured: the payload plus headroom is the ceiling, not the link.
  CHECK(rist_bandwidth_kbps(30000, std::optional<int> {100000}, false)
        == 35000);
  // 0 is "no measurement this launch", not a headroom of zero.
  CHECK(rist_bandwidth_kbps(30000, std::optional<int> {0}, false) == 35000);
}
