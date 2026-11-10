// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// Task 4c: the per-peer display state. These are the properties that make the
// panes stop alternating: one row per peer identity, a stable order, a stable
// aggregate, and stale legs dropped rather than frozen.

#include <cstdint>
#include <string>

#include "stats/peer_table.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
peer_stats::sample make(uint32_t id,
                        const std::string& cname,
                        double quality,
                        uint32_t rtt,
                        std::uint64_t bandwidth,
                        std::uint64_t sent,
                        std::uint64_t retransmitted)
{
  peer_stats::sample s;
  s.peer_id = id;
  s.cname = cname;
  s.quality = quality;
  s.rtt_ms = rtt;
  s.bandwidth = bandwidth;
  s.sent = sent;
  s.retransmitted = retransmitted;
  return s;
}
}  // namespace

TEST_CASE("two peers keep two rows whatever order they report in", "[peer_stats]")
{
  peer_stats::registry registry;
  registry.observe(make(1, "fibre", 99.0, 20, 5000, 100, 1), 1000);
  registry.observe(make(2, "lte", 71.0, 180, 1200, 40, 7), 1100);
  // The second tick arrives in the opposite order: the rows must not swap or
  // collapse into one.
  registry.observe(make(2, "lte", 70.0, 190, 1100, 60, 9), 2000);
  registry.observe(make(1, "fibre", 98.0, 21, 5100, 160, 1), 2100);

  const auto& rows = registry.peers();
  REQUIRE(rows.size() == 2);
  REQUIRE(rows[0].peer_id == 1);
  REQUIRE(rows[1].peer_id == 2);
  // Each row carries ITS peer's latest sample, not the last callback's.
  REQUIRE(rows[0].quality == 98.0);
  REQUIRE(rows[0].rtt_ms == 21);
  REQUIRE(rows[1].quality == 70.0);
  REQUIRE(rows[1].rtt_ms == 190);
  REQUIRE(rows[1].label == "lte");
}

TEST_CASE("the aggregate is the bond, not the last leg", "[peer_stats]")
{
  peer_stats::registry registry;
  registry.observe(make(1, "fibre", 99.0, 20, 5000, 100, 1), 1000);
  registry.observe(make(2, "lte", 60.0, 240, 1000, 40, 12), 1000);

  const peer_stats::aggregate total = registry.total();
  REQUIRE(total.any);
  REQUIRE(total.peers == 2);
  // Counters add up; quality and RTT report the worst leg, so a healthy leg
  // cannot mask a dead one.
  REQUIRE(total.bandwidth == 6000);
  REQUIRE(total.sent == 140);
  REQUIRE(total.retransmitted == 13);
  REQUIRE(total.quality == 60.0);
  REQUIRE(total.rtt_ms == 240);

  // Feeding the same set again in a different order changes nothing.
  registry.observe(make(2, "lte", 60.0, 240, 1000, 40, 12), 2000);
  registry.observe(make(1, "fibre", 99.0, 20, 5000, 100, 1), 2000);
  const peer_stats::aggregate again = registry.total();
  REQUIRE(again.bandwidth == total.bandwidth);
  REQUIRE(again.quality == total.quality);
  REQUIRE(again.rtt_ms == total.rtt_ms);
}

TEST_CASE("a peer that stops reporting is dropped, not frozen", "[peer_stats]")
{
  peer_stats::registry registry;
  registry.observe(make(1, "fibre", 99.0, 20, 5000, 100, 1), 1000);
  registry.observe(make(2, "lte", 60.0, 240, 1000, 40, 12), 1000);
  // Tick at t=4000: peer 1 reported, peer 2 did not.
  registry.observe(make(1, "fibre", 99.0, 20, 5000, 200, 1), 4000);
  registry.prune(4000, 1500);

  const auto& rows = registry.peers();
  REQUIRE(rows.size() == 1);
  REQUIRE(rows[0].peer_id == 1);
  REQUIRE(registry.total().peers == 1);
}

TEST_CASE("a peer with no cname still gets a row", "[peer_stats]")
{
  peer_stats::registry registry;
  registry.observe(make(0, "", 100.0, 5, 9000, 10, 0), 1000);
  REQUIRE(registry.peers().size() == 1);
  REQUIRE(registry.peers()[0].label == "peer 0");
  REQUIRE(peer_stats::format_row(registry.peers()[0])
              .find("peer 0") != std::string::npos);
}

TEST_CASE("a formatted row carries every column", "[peer_stats]")
{
  peer_stats::registry registry;
  registry.observe(make(3, "lte", 88.0, 120, 2500000, 900, 4), 1000);
  const std::string row = peer_stats::format_row(registry.peers()[0]);
  REQUIRE(row.find("lte") != std::string::npos);
  REQUIRE(row.find("Q 88%") != std::string::npos);
  REQUIRE(row.find("120 ms") != std::string::npos);
  REQUIRE(row.find("2500 Mb") != std::string::npos);
  REQUIRE(row.find("900 pkt") != std::string::npos);
  REQUIRE(row.find("4 retx") != std::string::npos);
}
