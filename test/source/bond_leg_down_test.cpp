// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// A leg going away, seen from the sending side.
//
// The product exists to survive a link dropping mid-event, so the property has
// to be measured rather than inferred from the code: with two bound legs
// sending, take one leg's interface away and keep feeding the transport. A
// single false from send_buffer is the whole stream stopping, because the
// encoder treats it as fatal -- so the assertion is that it never happens.
//
// Privilege split on purpose: this program never touches the host. The operator
// takes the link down from another shell while it runs, which is why it is
// driven by environment variables and skipped by default:
//
//   sudo ip link add obc-leg0 type dummy
//   sudo ip addr add 10.99.9.2/24 dev obc-leg0
//   sudo ip link set obc-leg0 up
//   OBC_LEGDOWN_ADDRESS=10.99.9.1:45620 OBC_LEGDOWN_LEGS=lo,obc-leg0 \
//     OBC_LEGDOWN_SECONDS=12 ./bond_leg_down_test
//   (in another shell, a few seconds in) sudo ip link set obc-leg0 down

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

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

auto env_value(const char* name) -> std::string
{
  const char* raw = std::getenv(name);
  return (raw == nullptr) ? std::string {} : std::string {raw};
}

auto split_legs(const std::string& list) -> std::vector<std::string>
{
  std::vector<std::string> legs;
  std::string current;
  for (const char c : list) {
    if (c == ',') {
      if (!current.empty()) {
        legs.push_back(current);
      }
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  if (!current.empty()) {
    legs.push_back(current);
  }
  return legs;
}
}  // namespace

TEST_CASE("a leg going down does not end the stream", "[bond][live][manual]")
{
  const std::string target = env_value("OBC_LEGDOWN_ADDRESS");
  const std::vector<std::string> legs =
      split_legs(env_value("OBC_LEGDOWN_LEGS"));
  if (target.empty() || legs.size() < 2) {
    SKIP(
        "set OBC_LEGDOWN_ADDRESS=host:port and OBC_LEGDOWN_LEGS=a,b to arm "
        "this; the operator takes a leg down mid-run");
  }

  const int seconds = std::atoi(env_value("OBC_LEGDOWN_SECONDS").c_str());
  const auto window = std::chrono::seconds {seconds > 0 ? seconds : 10};

  const auto split = target.find_last_of(':');
  REQUIRE(split != std::string::npos);

  output_config oc;
  oc.address = target;
  oc.host = target.substr(0, split);
  oc.port = static_cast<std::uint16_t>(std::atoi(target.c_str() + split + 1));
  oc.bandwidth = 20000;
  for (const std::string& name : legs) {
    oc.uplinks.push_back(uplink::config {name, true, 100});
  }

  transport sender;
  sender.set_log_callback(&capture_log);
  REQUIRE(sender.setup_rist_sender(oc));

  // One RIST payload per send, the size the transport itself chunks to.
  const std::vector<std::uint8_t> payload(1316, 0x5a);
  const auto start = std::chrono::steady_clock::now();
  auto next_report = start + std::chrono::seconds {1};

  std::uint64_t sent = 0;
  std::uint64_t failed = 0;
  std::uint64_t first_failure_second = 0;

  while (std::chrono::steady_clock::now() - start < window) {
    if (!sender.send_buffer(payload, 0)) {
      if (failed == 0) {
        first_failure_second = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start)
                .count());
      }
      ++failed;
    }
    ++sent;

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_report) {
      std::cout << "t="
                << std::chrono::duration_cast<std::chrono::seconds>(now - start)
                       .count()
                << "s sent=" << sent << " failed=" << failed << "\n";
      next_report += std::chrono::seconds {1};
    }

    // ~50 payloads a second: enough to keep both peers fed without pinning a
    // core on a box that is also running the build.
    std::this_thread::sleep_for(std::chrono::milliseconds {20});
  }

  std::cout << "total sent=" << sent << " failed=" << failed << "\n";
  if (failed != 0) {
    std::cout << "first failure at " << first_failure_second << "s\n";
  }

  // The assertion alone would also pass if the dying leg had never carried
  // anything, so the evidence is printed: which devices librist bound, and what
  // it said when the operator took one away.
  if (!env_value("OBC_LEGDOWN_VERBOSE").empty() || failed != 0) {
    std::lock_guard<std::mutex> guard(log_mtx);
    std::cout << "librist log (" << log_lines.size() << " lines):\n";
    for (const std::string& line : log_lines) {
      std::cout << "log: " << line << "\n";
    }
  }

  // The property: the transport kept accepting media after a leg disappeared.
  CHECK(failed == 0);
  CHECK(sent > 0);
}
