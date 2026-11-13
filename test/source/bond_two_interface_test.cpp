// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// A real two-interface run: two peers of one sender, each bound to a different
// local interface. librist logs the device it binds ("Binding socket to <dev>")
// and logs an error and carries on over the default route when the bind fails,
// so the log is the only place the bind can be observed from this side.

#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "RISTNet.h"
#include "lib/lib.h"
#include "net/adapters.h"
#include "transport/transport.h"
#include "transport/uplink.h"

namespace
{
std::mutex log_mtx;
std::vector<std::string> log_lines;

auto capture_log(void* /*arg*/, enum rist_log_level /*level*/, const char* text)
    -> int
{
  std::lock_guard<std::mutex> guard(log_mtx);
  log_lines.emplace_back(text == nullptr ? "" : text);
  return 0;
}

void clear_log()
{
  std::lock_guard<std::mutex> guard(log_mtx);
  log_lines.clear();
}

auto logged(const std::string& needle) -> bool
{
  std::lock_guard<std::mutex> guard(log_mtx);
  for (const std::string& line : log_lines) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// librist's own lines for the sockets it pinned, so a human running this sees
// the two devices rather than a green tick.
void print_bind_lines()
{
  std::lock_guard<std::mutex> guard(log_mtx);
  std::cout << "librist bind log:\n";
  for (const std::string& line : log_lines) {
    if (line.find("Binding socket to") != std::string::npos
        || line.find("Couldn't bind") != std::string::npos)
    {
      std::cout << "  " << line;
    }
  }
}

// Loopback plus one more adapter that can hold a leg. Both must be up and carry
// an IPv4: a leg on an adapter without an address cannot be pinned.
auto pick_two_adapters() -> std::optional<std::pair<net::adapter, net::adapter>>
{
  const std::vector<net::adapter> adapters = net::list();
  std::optional<net::adapter> loopback;
  std::optional<net::adapter> other;
  for (const net::adapter& a : adapters) {
    if (!a.up || a.ipv4.empty()) {
      continue;
    }
    if (a.name == "lo") {
      loopback = a;
    } else if (!other.has_value()) {
      other = a;
    }
  }
  if (!loopback.has_value() || !other.has_value()) {
    return std::nullopt;
  }
  return std::make_pair(*loopback, *other);
}
}  // namespace

TEST_CASE("two uplinks really bind two different interfaces", "[bond][live]")
{
  const auto pair = pick_two_adapters();
  if (!pair.has_value()) {
    SKIP("needs loopback plus one more up adapter with an IPv4 address");
  }
  const net::adapter& loopback = pair->first;
  const net::adapter& other = pair->second;

  output_config oc;
  oc.host = other.ipv4;  // a destination both legs can plausibly reach
  oc.port = 45600;
  oc.bandwidth = 20000;

  std::string error;
  const auto legs =
      uplink::resolve({{loopback.name, false, 90}, {other.name, false, 10}},
                      net::list(),
                      net::host_bind_style,
                      error);
  REQUIRE(error.empty());
  REQUIRE(legs.size() == 2);
  // The device bind is what forces egress here; the IP form is the Windows
  // path and is not accepted as a substitute on this platform.
  REQUIRE(net::host_bind_style == net::bind_style::device_name);
  REQUIRE(legs[0].miface == loopback.name);
  REQUIRE(legs[1].miface == other.name);

  std::vector<std::tuple<std::string, int>> urls;
  for (const uplink::leg& leg : legs) {
    urls.emplace_back(uplink::build_url(oc, leg, oc.bandwidth),
                      static_cast<int>(leg.weight.value_or(0)));
  }
  INFO("leg 1 URL: " << std::get<0>(urls[0]));
  INFO("leg 2 URL: " << std::get<0>(urls[1]));
  CHECK(std::get<0>(urls[0]).find("miface=" + loopback.name)
        != std::string::npos);
  CHECK(std::get<0>(urls[1]).find("miface=" + other.name) != std::string::npos);

  clear_log();
  RISTNetSender sender;
  RISTNetSender::RISTNetSenderSettings sender_settings;
  sender_settings.mLogLevel = RIST_LOG_INFO;
  sender_settings.mLogSetting->log_cb = capture_log;
  sender_settings.mProfile = RIST_PROFILE_ADVANCED;
  REQUIRE(sender.initSender(urls, sender_settings));

  // librist says which device each socket was pinned to. A failed bind is
  // logged as "Couldn't bind to <dev>" and the peer carries on unbound, so the
  // absence of that line is part of the assertion.
  CHECK(logged("Binding socket to " + loopback.name));
  CHECK(logged("Binding socket to " + other.name));
  CHECK_FALSE(logged("Couldn't bind"));
  print_bind_lines();

  // The wrapper's ids must be the ones the sender stats report, and both legs
  // must be distinct peers of the one session.
  const std::uint32_t first_id = sender.getPeerId(0);
  const std::uint32_t second_id = sender.getPeerId(1);
  CHECK(first_id != 0);
  CHECK(second_id != 0);
  CHECK(first_id != second_id);
  CHECK(sender.getPeerId(2) == 0);

  // And the live weight path reaches librist for each leg.
  CHECK(sender.setPeerWeight(first_id, 60));
  CHECK(sender.setPeerWeight(second_id, 40));
  CHECK_FALSE(sender.setPeerWeight(0xFFFFFFFFu, 50));

  sender.destroySender();
}

TEST_CASE("the transport sends one peer per uplink and states each leg",
          "[bond][live]")
{
  const auto pair = pick_two_adapters();
  if (!pair.has_value()) {
    SKIP("needs loopback plus one more up adapter with an IPv4 address");
  }
  const net::adapter& loopback = pair->first;
  const net::adapter& other = pair->second;

  output_config oc;
  oc.address = other.ipv4 + ":45601";
  oc.host = other.ipv4;
  oc.port = 45601;
  oc.bandwidth = 20000;
  oc.uplinks = {
      uplink::config {loopback.name, true, 100},
      uplink::config {other.name, true, 100},
  };

  clear_log();
  {
    transport sender;
    sender.set_log_callback(&capture_log);
    REQUIRE(sender.setup_rist_sender(oc));

    // What this side asked for, per leg: the auto legs state the equal share
    // the controller starts from, and the device is named for the bind.
    CHECK(logged("[bond] leg 1/2 " + loopback.name + ": miface=" + loopback.name
                 + " weight=" + std::to_string(uplink::k_start_weight)));
    CHECK(logged("[bond] leg 2/2 " + other.name + ": miface=" + other.name
                 + " weight=" + std::to_string(uplink::k_start_weight)));
    // What librist did with it.
    CHECK(logged("Binding socket to " + loopback.name));
    CHECK(logged("Binding socket to " + other.name));
    CHECK_FALSE(logged("Couldn't bind"));
    print_bind_lines();
  }

  // An empty list is the single default-route leg: no miface, no weight, which
  // is the URL this encoder sent before bonding existed.
  output_config plain;
  plain.address = "127.0.0.1:45602";
  plain.host = "127.0.0.1";
  plain.port = 45602;
  clear_log();
  {
    transport sender;
    sender.set_log_callback(&capture_log);
    REQUIRE(sender.setup_rist_sender(plain));
    CHECK(logged("[bond] leg 1/1 default: default route"));
    CHECK_FALSE(logged("miface="));
  }
}
