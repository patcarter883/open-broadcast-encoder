// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The log filter that keeps a dead bonded leg from burying the transport pane.
//
// Pure string and clock logic, no socket and no pipeline: the behaviour worth
// pinning is which librist lines survive, how often a summary appears, and that
// the burst ends with its totals rather than merely going quiet.

#include <cstdint>
#include <string>
#include <vector>

#include "transport/librist_log_filter.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
// The two lines librist emits per packet it cannot send. The second carries a
// sequence number, so it never repeats byte-for-byte.
constexpr const char* kSendFailed =
    "Send failed via main-profile sendmsg: errno=101, size=1340, socket=4\n";
constexpr const char* kSendtoFailed =
    "\tError on transmission sendto, ret=-1 to 10.99.9.1:45621/32769, seq "
    "#378, "
    "1316 bytes\n";

auto count(const std::vector<std::string>& lines) -> std::size_t
{
  return lines.size();
}
}  // namespace

TEST_CASE("a per-packet send failure is not forwarded", "[rist][log]")
{
  librist_log_filter filter;
  CHECK(filter.on_line(kSendFailed, 0).empty());
  CHECK(filter.on_line(kSendtoFailed, 1).empty());
}

TEST_CASE("other librist lines pass through untouched", "[rist][log]")
{
  librist_log_filter filter;
  const std::string line = "Binding socket to eth0\n";
  const auto out = filter.on_line(line, 0);
  REQUIRE(count(out) == 1);
  CHECK(out[0] == line);
}

TEST_CASE("a burst becomes one summary per second", "[rist][log]")
{
  librist_log_filter filter;
  std::size_t summaries = 0;

  // Ten seconds of a dead leg at 100 failures a second: without the filter that
  // is 2000 lines; with it, ten summaries.
  for (std::int64_t ms = 0; ms < 10000; ms += 5) {
    for (const auto& line : filter.on_line(kSendFailed, ms)) {
      CHECK(line.find("send failure") != std::string::npos);
      CHECK(line.find("errno=101: Network is unreachable")
            != std::string::npos);
      ++summaries;
    }
  }

  CHECK(summaries == 9);
}

TEST_CASE("the burst ends with its totals, once", "[rist][log]")
{
  librist_log_filter filter;

  // Three seconds of failures, then ordinary lines from the surviving leg --
  // which must NOT be read as a recovery.
  for (std::int64_t ms = 0; ms < 3000; ms += 5) {
    (void)filter.on_line(kSendFailed, ms);
  }

  const auto during = filter.on_line("Normal line while still failing\n", 3000);
  REQUIRE(count(during) == 1);
  CHECK(during[0] == "Normal line while still failing\n");

  // Quiet for the full period: now the totals are reported, once, and the
  // line that triggered it still goes through.
  const auto closing = filter.on_line("Normal line again\n", 6000);
  REQUIRE(count(closing) == 2);
  CHECK(closing[0].find("send failures stopped after") != std::string::npos);
  CHECK(closing[0].find("packet(s)") != std::string::npos);
  CHECK(closing[1] == "Normal line again\n");

  const auto after = filter.on_line("Another line\n", 6001);
  REQUIRE(count(after) == 1);
  CHECK(after[0] == "Another line\n");
}

TEST_CASE("an unknown errno is reported as a number, not invented",
          "[rist][log]")
{
  librist_log_filter filter;
  const std::string odd =
      "Send failed via main-profile sendmsg: errno=42, size=1340, socket=4\n";
  (void)filter.on_line(odd, 0);
  const auto out = filter.on_line(odd, 1100);
  REQUIRE(count(out) == 1);
  CHECK(out[0].find("errno=42") != std::string::npos);
  CHECK(out[0].find("Network is unreachable") == std::string::npos);
}
