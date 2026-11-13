// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The bonding decisions that must be pinned without a pipeline: the URL each
// uplink produces, the adapter-name-to-miface rule on both platform styles, and
// the stats-to-weight mapping.

#include <cstdint>
#include <string>
#include <vector>

#include "transport/uplink.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lib/lib.h"
#include "net/adapters.h"
#include "stats/bond_weight.h"

namespace
{
auto bare_output() -> output_config
{
  output_config oc;
  oc.address = "203.0.113.5:5000";
  oc.host = "203.0.113.5";
  oc.port = 5000;
  oc.bandwidth = 20000;
  oc.buffer_min = 1000;
  oc.buffer_max = 5000;
  oc.rtt_min = 40;
  oc.rtt_max = 500;
  oc.reorder_buffer = 30;
  return oc;
}

auto fake_adapters() -> std::vector<net::adapter>
{
  return {
      {"lo", "127.0.0.1", true},
      {"wwan0", "10.8.0.2", true},
      {"wwan1", "10.9.0.2", true},
      {"ipv6only", "", true},
  };
}
}  // namespace

TEST_CASE("the single implicit leg builds the URL this encoder has always sent",
          "[uplink]")
{
  std::string error;
  const auto legs =
      uplink::resolve({}, fake_adapters(), net::bind_style::device_name, error);

  REQUIRE(error.empty());
  REQUIRE(legs.size() == 1);
  CHECK(legs.front().label == "default");
  CHECK(legs.front().miface.empty());
  CHECK_FALSE(legs.front().weight.has_value());

  // No miface, no weight: byte for byte the pre-bonding URL, so an operator
  // with no uplink configured cannot notice the bonding code exists.
  CHECK(uplink::build_url(bare_output(), legs.front(), bare_output().bandwidth)
        == "rist://203.0.113.5:5000?bandwidth=20000&buffer-min=1000"
           "&buffer-max=5000&rtt-min=40&rtt-max=500&reorder-buffer=30"
           "&timing-mode=0");
}

TEST_CASE("each uplink's URL pins its device and states its weight", "[uplink]")
{
  const output_config oc = bare_output();
  std::vector<uplink::config> configured {
      {"wwan0", false, 90},
      {"wwan1", false, 10},
  };
  std::string error;
  const auto legs = uplink::resolve(
      configured, fake_adapters(), net::bind_style::device_name, error);
  REQUIRE(error.empty());
  REQUIRE(legs.size() == 2);

  CHECK(uplink::build_url(oc, legs[0], oc.bandwidth)
        == "rist://203.0.113.5:5000?bandwidth=20000&buffer-min=1000"
           "&buffer-max=5000&rtt-min=40&rtt-max=500&reorder-buffer=30"
           "&timing-mode=0&miface=wwan0&weight=90");
  CHECK(uplink::build_url(oc, legs[1], oc.bandwidth)
            .ends_with("&miface=wwan1&weight=10"));

  // Both legs are peers on the SAME session port: bonded links are one flow.
  CHECK(uplink::build_url(oc, legs[0], oc.bandwidth)
            .rfind("rist://203.0.113.5:5000?", 0)
        == 0);
  CHECK(uplink::build_url(oc, legs[1], oc.bandwidth)
            .rfind("rist://203.0.113.5:5000?", 0)
        == 0);

  // The host's LISTEN form never reaches a sender URL.
  output_config listen_form = oc;
  listen_form.host = "@203.0.113.5";
  CHECK(uplink::build_url(listen_form, legs[0], listen_form.bandwidth)
            .rfind("rist://203.0.113.5:", 0)
        == 0);
}

TEST_CASE("an auto leg starts from the equal share and keeps the flag",
          "[uplink]")
{
  std::vector<uplink::config> configured {
      {"wwan0", true, 7},
      {"wwan1", true, 7},
  };
  std::string error;
  const auto legs = uplink::resolve(
      configured, fake_adapters(), net::bind_style::device_name, error);
  REQUIRE(error.empty());
  for (const uplink::leg& leg : legs) {
    CHECK(leg.auto_weight);
    // The fixed weight is ignored while auto: the URL states the equal share
    // the controller measures from, and that number is logged at Start.
    REQUIRE(leg.weight.has_value());
    CHECK(*leg.weight == uplink::k_start_weight);
  }
}

TEST_CASE("an uplink that resolves to nothing refuses the whole list",
          "[uplink]")
{
  std::string error;
  const auto legs = uplink::resolve({{"wwan9", false, 50}},
                                    fake_adapters(),
                                    net::bind_style::device_name,
                                    error);
  CHECK(legs.empty());
  CHECK(error.find("wwan9") != std::string::npos);

  // An IPv4 literal is accepted as written: it binds the source address, and
  // it is the only form that works when the address is not a local adapter.
  std::string literal_error;
  const auto literal = uplink::resolve({{"192.0.2.9", false, 50}},
                                       fake_adapters(),
                                       net::bind_style::device_name,
                                       literal_error);
  REQUIRE(literal_error.empty());
  REQUIRE(literal.size() == 1);
  CHECK(literal.front().miface == "192.0.2.9");
}

TEST_CASE("a fixed weight below 1 is raised to 1, never left at duplicate",
          "[uplink]")
{
  std::string error;
  const auto legs = uplink::resolve({{"wwan0", false, 0}},
                                    fake_adapters(),
                                    net::bind_style::device_name,
                                    error);
  REQUIRE(error.empty());
  REQUIRE(legs.size() == 1);
  REQUIRE(legs.front().weight.has_value());
  CHECK(*legs.front().weight == 1);
}

TEST_CASE("adapter names resolve to a device name or to a source address",
          "[adapters]")
{
  const auto adapters = fake_adapters();

  // Linux/macOS: the device name is what SO_BINDTODEVICE / IP_BOUND_IF take.
  CHECK(net::miface_for(adapters, "wwan0", net::bind_style::device_name)
        == "wwan0");
  // Windows: no device bind, the source address is what forces egress.
  CHECK(net::miface_for(adapters, "wwan0", net::bind_style::source_address)
        == "10.8.0.2");

  // An adapter with no IPv4 cannot carry a leg on Windows; the same adapter is
  // still a valid device on Linux.
  CHECK(net::miface_for(adapters, "ipv6only", net::bind_style::source_address)
            .empty());
  CHECK(net::miface_for(adapters, "ipv6only", net::bind_style::device_name)
        == "ipv6only");

  // A literal passes through on both styles; anything else resolves to nothing
  // so the caller refuses rather than sending over the default route.
  CHECK(net::miface_for(adapters, "10.8.0.99", net::bind_style::device_name)
        == "10.8.0.99");
  CHECK(net::miface_for(adapters, "10.8.0.99", net::bind_style::source_address)
        == "10.8.0.99");
  CHECK(
      net::miface_for(adapters, "wwan", net::bind_style::device_name).empty());
  CHECK(net::miface_for(adapters, "", net::bind_style::device_name).empty());

  CHECK(net::is_ipv4_literal("10.8.0.2"));
  CHECK_FALSE(net::is_ipv4_literal("wwan0"));
  CHECK_FALSE(net::is_ipv4_literal("10.8.0.2/24"));
}

TEST_CASE("the leg's measured upload becomes its weight", "[bond_weight]")
{
  // 125000 B/s carried with half the rotation = 1000 kbps at the share, so
  // 2000 kbps with the whole rotation; a clean leg at the RTT floor takes all
  // of it: 2000 kbps / 100 kbps per unit = weight 20.
  bond_weight::leg_sample sample;
  sample.bandwidth_bytes_per_s = 125000;
  sample.rtt_ms = 40;
  sample.sent = 3000;

  auto est = bond_weight::estimate_leg(sample, 0.5);
  CHECK(est.available_kbps == Catch::Approx(2000.0));
  CHECK(est.weight == 20);

  // Same link, same share, but a third of the packets had to be retransmitted:
  // the figure in the stats includes that overhead, so only 75% of it is
  // available for payload.
  bond_weight::leg_sample lossy = sample;
  lossy.retransmitted = 1000;
  est = bond_weight::estimate_leg(lossy, 0.5);
  CHECK(est.available_kbps == Catch::Approx(1500.0));
  CHECK(est.weight == 15);

  // 400 ms against a 100 ms floor: the retransmit window drains four times
  // slower, so only a quarter of the capacity can be used.
  bond_weight::leg_sample high_rtt = sample;
  high_rtt.rtt_ms = 400;
  est = bond_weight::estimate_leg(high_rtt, 0.5);
  CHECK(est.available_kbps == Catch::Approx(500.0));
  CHECK(est.weight == 5);

  // A leg that carried nothing is given the floor, never 0: librist reads 0 as
  // "duplicate every packet on this leg".
  bond_weight::leg_sample idle;
  idle.rtt_ms = 40;
  est = bond_weight::estimate_leg(idle, 0.5);
  CHECK(est.weight == bond_weight::k_min_weight);
  CHECK(est.weight != 0);

  // A very fast leg is capped, so one gigabit path cannot swamp the bond.
  bond_weight::leg_sample fast;
  fast.bandwidth_bytes_per_s = 10000000;  // 80 Mbit/s
  fast.rtt_ms = 40;
  fast.sent = 10000;
  est = bond_weight::estimate_leg(fast, 0.5);
  CHECK(est.weight == bond_weight::k_max_weight);
}

TEST_CASE("a leg's share is what its carried figure is extrapolated from",
          "[bond_weight]")
{
  // The same carried rate under a smaller share implies a larger capacity, so
  // a leg the balancer is starving is not read as a slow leg.
  bond_weight::leg_sample sample;
  sample.bandwidth_bytes_per_s = 62500;  // 500 kbps carried
  sample.rtt_ms = 40;
  sample.sent = 1000;

  const auto half = bond_weight::estimate_leg(sample, 0.5);
  const auto tenth = bond_weight::estimate_leg(sample, 0.1);
  CHECK(half.weight == 10);  // 1000 kbps available
  CHECK(tenth.weight == 50);  // 5000 kbps available
  CHECK(tenth.weight > half.weight);
}
