// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The bridge joins a multicast listen group on ONE interface, and libRIST takes
// that interface only as `miface=` inside the listen URL. These tests pin the
// URL this encoder builds and the order of authority for where the interface
// comes from -- all of it without a kernel lookup, so the result does not
// depend on the host running them.

#include <string>
#include <vector>

#include "bridge/join_interface.h"

#include <catch2/catch_test_macros.hpp>

using namespace bridge;

namespace
{
// Every lookup this file cares about is decided by the test, not by the routing
// table of the machine running it.
struct fake_detect
{
  std::string result;
  std::vector<std::string> asked;

  detect_fn fn()
  {
    return [this](const std::string& group)
    {
      asked.push_back(group);
      return result;
    };
  }
};
}  // namespace

TEST_CASE("the multicast group is read out of a listen url", "[bridge][join]")
{
  CHECK(multicast_group("rist://@239.1.1.10:5100") == "239.1.1.10");
  CHECK(multicast_group("rist://239.1.1.10:5100?miface=eth1") == "239.1.1.10");
  // 224.0.0.0/4 and nothing else.
  CHECK(multicast_group("rist://@224.0.1.129:5000") == "224.0.1.129");
  CHECK(multicast_group("rist://@239.255.255.250:5000") == "239.255.255.250");
  CHECK(multicast_group("rist://@240.1.1.10:5100").empty());
  CHECK(multicast_group("rist://@223.1.1.10:5100").empty());
  CHECK(multicast_group("rist://@192.168.8.1:6000").empty());
  CHECK(multicast_group("rist://0.0.0.0:5000").empty());
  CHECK(multicast_group("").empty());
}

TEST_CASE("the interface is appended to a multicast listen url only once",
          "[bridge][join]")
{
  CHECK(with_multicast_interface("rist://@239.1.1.10:5100", "eth1")
        == "rist://@239.1.1.10:5100?miface=eth1");
  // An existing query keeps its parameters, and the join is joined onto it.
  CHECK(with_multicast_interface("rist://@239.1.1.10:5100?buffer-max=5000",
                                 "eth1")
        == "rist://@239.1.1.10:5100?buffer-max=5000&miface=eth1");
  // Already joined: the operator's own value stands.
  CHECK(
      with_multicast_interface("rist://@239.1.1.10:5100?miface=br-lan", "eth1")
      == "rist://@239.1.1.10:5100?miface=br-lan");
  // Nothing to join, or nothing to join with.
  CHECK(with_multicast_interface("rist://0.0.0.0:5000", "eth1")
        == "rist://0.0.0.0:5000");
  CHECK(with_multicast_interface("rist://@239.1.1.10:5100", "")
        == "rist://@239.1.1.10:5100");
}

TEST_CASE("the join interface is dropped from a url this encoder sends to",
          "[bridge][join]")
{
  CHECK(without_multicast_interface("rist://239.1.1.10:5100?miface=eth1")
        == "rist://239.1.1.10:5100");
  CHECK(without_multicast_interface(
            "rist://239.1.1.10:5100?miface=eth1&buffer-max=5000")
        == "rist://239.1.1.10:5100?buffer-max=5000");
  CHECK(without_multicast_interface(
            "rist://239.1.1.10:5100?buffer-max=5000&miface=eth1")
        == "rist://239.1.1.10:5100?buffer-max=5000");
  CHECK(without_multicast_interface("rist://239.1.1.10:5100")
        == "rist://239.1.1.10:5100");
}

TEST_CASE(
    "the join interface resolves from the url, then the setting, then the "
    "route, then the uplink",
    "[bridge][join]")
{
  const std::string listen = "rist://@239.1.1.10:5100";

  SECTION("an interface in the url outranks every derived value")
  {
    fake_detect detect;
    detect.result = "from-route";
    CHECK(resolve_join_interface("rist://@239.1.1.10:5100?miface=br-lan",
                                 "configured",
                                 "uplink",
                                 detect.fn())
          == "br-lan");
    CHECK(detect.asked.empty());
  }

  SECTION("the setting outranks the route lookup")
  {
    fake_detect detect;
    detect.result = "from-route";
    CHECK(resolve_join_interface(listen, "configured", "uplink", detect.fn())
          == "configured");
    CHECK(detect.asked.empty());
  }

  SECTION("an empty setting is resolved from the route to the group")
  {
    fake_detect detect;
    detect.result = "from-route";
    CHECK(resolve_join_interface(listen, "", "uplink", detect.fn())
          == "from-route");
    REQUIRE(detect.asked.size() == 1);
    CHECK(detect.asked.at(0) == "239.1.1.10");
  }

  SECTION("a lookup that names nothing falls back to the bridge's uplink")
  {
    fake_detect detect;  // empty result
    CHECK(resolve_join_interface(listen, "", "wwan0", detect.fn()) == "wwan0");
  }

  SECTION("nothing is invented when nothing can be named")
  {
    fake_detect detect;
    CHECK(resolve_join_interface(listen, "", "", detect.fn()).empty());
    // And the url is then actuated exactly as written.
    CHECK(with_multicast_interface(
              listen, resolve_join_interface(listen, "", "", detect.fn()))
          == listen);
  }

  SECTION("a unicast listen leg has no join at all")
  {
    fake_detect detect;
    detect.result = "from-route";
    CHECK(resolve_join_interface(
              "rist://0.0.0.0:5000", "configured", "uplink", detect.fn())
              .empty());
    CHECK(detect.asked.empty());
  }
}

TEST_CASE("a multicast listen leg with no join interface is reported",
          "[bridge][join]")
{
  const std::string warning = join_warning("rist://@239.1.1.10:5100");
  CHECK_FALSE(warning.empty());
  CHECK(warning.find("239.1.1.10") != std::string::npos);

  // Nothing to report once it is joined, and nothing to report for a leg that
  // has no group.
  CHECK(join_warning("rist://@239.1.1.10:5100?miface=eth1").empty());
  CHECK(join_warning("rist://0.0.0.0:5000").empty());
}

TEST_CASE("the lookup names an interface of this host or nothing",
          "[bridge][join]")
{
  // The real lookup: a multicast address routes through some interface on
  // practically every host, and a non-multicast address must never produce one.
  CHECK(detect_multicast_interface("192.168.8.1").empty());
  CHECK(detect_multicast_interface("not-an-address").empty());
}
