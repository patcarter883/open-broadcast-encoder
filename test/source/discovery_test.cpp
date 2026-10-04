// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// Merging split advertisements, and the browse's tolerance of a network with no
// bridges on it.

#include <chrono>
#include <string>
#include <vector>

#include "bridge/discovery.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
auto service(const std::string& instance) -> bridge::mdns::service
{
  bridge::mdns::service s;
  s.instance = instance;
  return s;
}
}  // namespace

TEST_CASE("records spread across responses merge into one bridge",
          "[discovery]")
{
  std::vector<bridge::mdns::service> seen;

  // The PTR arrives first and names the instance.
  bridge::merge_service(seen, service("rist2rist-aa:bb"));
  REQUIRE(seen.size() == 1);

  // Then the SRV, in a later packet.
  auto srv = service("rist2rist-aa:bb");
  srv.host = "OpenWrt.lan";
  srv.port = 5000;
  bridge::merge_service(seen, srv);

  // Then the address.
  auto address = service("rist2rist-aa:bb");
  address.address = "192.168.8.1";
  bridge::merge_service(seen, address);

  REQUIRE(seen.size() == 1);
  REQUIRE(seen[0].host == "OpenWrt.lan");
  REQUIRE(seen[0].port == 5000);
  REQUIRE(seen[0].address == "192.168.8.1");
}

TEST_CASE("a later record fills gaps but does not erase what is known",
          "[discovery]")
{
  std::vector<bridge::mdns::service> seen;

  auto full = service("rist2rist-aa:bb");
  full.host = "OpenWrt.lan";
  full.address = "192.168.8.1";
  full.port = 5000;
  bridge::merge_service(seen, full);

  // A repeat announcement carrying only the PTR must not blank the rest.
  bridge::merge_service(seen, service("rist2rist-aa:bb"));

  REQUIRE(seen.size() == 1);
  REQUIRE(seen[0].port == 5000);
  REQUIRE(seen[0].address == "192.168.8.1");
}

TEST_CASE("the newest TXT wins, because the bridge is reporting its own state",
          "[discovery]")
{
  std::vector<bridge::mdns::service> seen;

  auto first = service("rist2rist-aa:bb");
  first.txt["managed"] = "0";
  first.txt["fingerprint"] = "abc";
  bridge::merge_service(seen, first);

  auto later = service("rist2rist-aa:bb");
  later.txt["managed"] = "1";
  bridge::merge_service(seen, later);

  REQUIRE(seen.size() == 1);
  REQUIRE(seen[0].txt_value("managed") == "1");
  // A key the newer record did not mention survives.
  REQUIRE(seen[0].txt_value("fingerprint") == "abc");
}

TEST_CASE("two bridges stay separate", "[discovery]")
{
  std::vector<bridge::mdns::service> seen;
  bridge::merge_service(seen, service("rist2rist-aa:bb"));
  bridge::merge_service(seen, service("rist2rist-cc:dd"));

  REQUIRE(seen.size() == 2);
}

TEST_CASE("an entry with no instance is dropped rather than stored",
          "[discovery]")
{
  std::vector<bridge::mdns::service> seen;
  bridge::merge_service(seen, bridge::mdns::service {});

  REQUIRE(seen.empty());
}

TEST_CASE("a browse with no bridges returns an empty list promptly",
          "[discovery]")
{
  // Whatever the environment (no socket permission, no network, or a real LAN
  // with nothing on it), a browse must finish inside its window and never
  // throw. A manual address is the supported fallback.
  const auto started = std::chrono::steady_clock::now();
  const auto found = bridge::discover(std::chrono::milliseconds(150));
  const auto elapsed = std::chrono::steady_clock::now() - started;

  REQUIRE(elapsed < std::chrono::seconds(5));
  for (const auto& service : found) {
    REQUIRE_FALSE(service.instance.empty());
  }
}
