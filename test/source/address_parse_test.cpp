// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The RIST Address field is free text and it is the ONLY statement of the
// destination the operator can see, so what it parses to decides where the
// stream goes. Splitting on the first ':' read "rist://host:20000" as host
// "rist" + port 5000: a destination nothing answers, reported as healthy
// sending.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "lib/address.h"

namespace
{

struct expectation
{
  std::string input;
  std::string host;
  int port;
};

}  // namespace

TEST_CASE("a scheme is not part of the host", "[address]")
{
  auto parsed =
      obr::parse_rist_address("rist://syd1-a.relay.example.net.au:20000");
  REQUIRE(parsed.host == "syd1-a.relay.example.net.au");
  REQUIRE(parsed.port == 20000);
}

TEST_CASE("a query string is not part of the destination", "[address]")
{
  auto parsed = obr::parse_rist_address(
      "rist://syd1-a.relay.example.net.au:20000?bandwidth=20000&timing-mode=0");
  REQUIRE(parsed.host == "syd1-a.relay.example.net.au");
  REQUIRE(parsed.port == 20000);
}

TEST_CASE("a bare host:port parses without a scheme", "[address]")
{
  auto parsed = obr::parse_rist_address("relay.example.net.au:5100");
  REQUIRE(parsed.host == "relay.example.net.au");
  REQUIRE(parsed.port == 5100);
}

TEST_CASE("a host with no port keeps the RIST default", "[address]")
{
  auto parsed = obr::parse_rist_address("relay.example.net.au");
  REQUIRE(parsed.host == "relay.example.net.au");
  REQUIRE(parsed.port == 5000);
}

TEST_CASE("the listen marker stays on the host", "[address]")
{
  auto parsed =
      obr::parse_rist_address("rist://@239.1.1.10:5100?miface=eth1");
  REQUIRE(parsed.host == "@239.1.1.10");
  REQUIRE(parsed.port == 5100);
}

TEST_CASE("bracketed IPv6 carries a port", "[address]")
{
  auto parsed = obr::parse_rist_address("rist://[2001:db8::1]:20000");
  REQUIRE(parsed.host == "[2001:db8::1]");
  REQUIRE(parsed.port == 20000);
}

TEST_CASE("a bare IPv6 literal is host-only", "[address]")
{
  auto parsed = obr::parse_rist_address("2001:db8::1");
  REQUIRE(parsed.host == "2001:db8::1");
  REQUIRE(parsed.port == 5000);
}

TEST_CASE("user info is not part of the host", "[address]")
{
  auto parsed = obr::parse_rist_address("rist://user:pw@relay.example.net.au:5000");
  REQUIRE(parsed.host == "relay.example.net.au");
  REQUIRE(parsed.port == 5000);
}

TEST_CASE("a bad port falls back to the default", "[address]")
{
  const expectation cases[] = {
      {"relay.example.net.au:0", "relay.example.net.au", 5000},
      {"relay.example.net.au:70000", "relay.example.net.au", 5000},
      {"relay.example.net.au:abc", "relay.example.net.au", 5000},
      {"relay.example.net.au:", "relay.example.net.au", 5000},
      {"rist://relay.example.net.au:5000/", "relay.example.net.au", 5000},
  };
  for (const auto& c : cases) {
    auto parsed = obr::parse_rist_address(c.input);
    REQUIRE(parsed.host == c.host);
    REQUIRE(parsed.port == c.port);
  }
}

TEST_CASE("an empty address names no host", "[address]")
{
  auto parsed = obr::parse_rist_address("");
  REQUIRE(parsed.host.empty());
  REQUIRE(parsed.port == 5000);
}
