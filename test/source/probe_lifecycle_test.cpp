// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The ramp's lifecycle and the decision to run it at all.
//
// The ramp drives the live RIST sender for seconds, so the two rules that
// matter are about ORDER and about RESTRAINT: End Session must be able to
// cancel and join a ramp before it deallocates the session the ramp belongs to,
// and an allocation that already has a usable figure must not send test traffic
// to a node that is already carrying the session.
//
// No sender, no UI, no portal: the ramp's body and the clock are injected.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "calibrate/probe_lifecycle.h"

#include <catch2/catch_test_macros.hpp>

#include "calibrate/probe_policy.h"
#include "lib/rist_bandwidth.h"

namespace
{
// The settings file's stamp format, in local time: what the ramp writes and
// what the policy reads back.
auto stamp_local(std::time_t when) -> std::string
{
  std::tm local {};
  if (::localtime_r(&when, &local) == nullptr) {
    return {};
  }
  char buffer[32] {};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local) == 0) {
    return {};
  }
  return buffer;
}

constexpr int k_encode_kbps = 6000;
const std::string k_destination = "rist://syd1-b.relay.example:5001";

// The destination an allocation just produced is a different address: a figure
// measured for the old one proves nothing about the new one.
const std::string k_other_destination = "rist://mel1-b.relay.example:5001";
}  // namespace

// The defect: a probe was running for a session, and End Session could not
// close that session until the ramp finished on its own. The close must cancel
// and JOIN the ramp first -- the ramp's thread is gone before the deallocate
// runs, so nothing it does can land on a session that no longer exists.
TEST_CASE("ending a session during a probe cancels and joins the ramp first",
          "[probe][lifecycle]")
{
  probe_lifecycle::worker probe;
  std::mutex events_mtx;
  std::vector<std::string> events;
  std::atomic<bool> ramp_saw_cancel {false};

  // A ramp that would keep going: only the cancel makes it return. Bounded so a
  // broken cancel fails an assertion instead of hanging the suite.
  REQUIRE(probe.start(
      [&probe, &events_mtx, &events, &ramp_saw_cancel]
      {
        for (int slice = 0; slice < 5000; ++slice) {
          if (probe.cancel_requested()) {
            ramp_saw_cancel.store(true, std::memory_order_release);
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::lock_guard<std::mutex> guard(events_mtx);
        events.emplace_back("ramp-returned");
      }));

  // Let the ramp get into its loop before End Session is pressed.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  const bool cancelled = probe_lifecycle::cancel_then(
      probe,
      [&events_mtx, &events]
      {
        std::lock_guard<std::mutex> guard(events_mtx);
        events.emplace_back("deallocated");
      });

  CHECK(cancelled);
  CHECK(ramp_saw_cancel.load(std::memory_order_acquire));
  // Joined, not merely asked: nothing of the ramp is still running.
  CHECK_FALSE(probe.running());
  // And the deallocate ran AFTER the ramp's thread returned.
  CHECK(events == std::vector<std::string> {"ramp-returned", "deallocated"});

  // The handle was reaped, so the next session can measure again.
  CHECK(probe.start([] {}));
  probe.stop();
}

// A repeat allocation on the same link must not re-probe: the measurement is a
// property of the link, and the ramp adds traffic to a node already at its
// ceiling.
TEST_CASE("an allocation with a fresh measurement runs no ramp",
          "[probe][policy]")
{
  const std::time_t now = 1'800'000'000;
  const std::string taken = stamp_local(now - 60);  // a minute ago
  REQUIRE_FALSE(taken.empty());

  int ramps = 0;
  for (int allocation = 0; allocation < 20; ++allocation) {
    if (probe_policy::decide(7400,
                             k_destination,
                             taken,
                             k_destination,
                             k_encode_kbps,
                             false,
                             now)
        == probe_policy::decision::run)
    {
      ++ramps;
    }
  }
  CHECK(ramps == 0);
  CHECK(
      probe_policy::decide(
          7400, k_destination, taken, k_destination, k_encode_kbps, false, now)
      == probe_policy::decision::fresh);
}

// A missing or stale figure is what the ramp is for -- exactly once, and the
// figure it writes then makes the next allocation skip it.
TEST_CASE("a missing or stale measurement runs the ramp once",
          "[probe][policy]")
{
  const std::time_t now = 1'800'000'000;
  std::optional<std::string> stored_taken;  // what is on disk, if anything
  int measured_kbps = 0;

  const auto allocate = [&]
  {
    const probe_policy::decision plan = probe_policy::decide(
        measured_kbps,
        stored_taken.has_value() ? k_destination : std::string {},
        stored_taken.value_or(std::string {}),
        k_destination,
        k_encode_kbps,
        false,
        now);
    if (plan != probe_policy::decision::run) {
      return false;
    }
    // The ramp ran and wrote its figure.
    measured_kbps = 7400;
    stored_taken = stamp_local(now);
    return true;
  };

  // Never measured: the first allocation probes, the next ones reuse the
  // figure.
  CHECK(allocate());
  CHECK_FALSE(allocate());
  CHECK_FALSE(allocate());

  // Inside the window is still fresh at its edge, stale one second past it.
  const std::time_t edge = now - probe_policy::k_fresh_for_seconds;
  CHECK(probe_policy::decide(7400,
                             k_destination,
                             stamp_local(edge),
                             k_destination,
                             k_encode_kbps,
                             false,
                             now)
        == probe_policy::decision::fresh);
  const std::time_t stale = now - probe_policy::k_fresh_for_seconds - 3600;
  CHECK(probe_policy::decide(7400,
                             k_destination,
                             stamp_local(stale),
                             k_destination,
                             k_encode_kbps,
                             false,
                             now)
        == probe_policy::decision::run);

  // A figure for another address is not evidence about this one.
  CHECK(probe_policy::decide(7400,
                             k_other_destination,
                             stamp_local(now),
                             k_destination,
                             k_encode_kbps,
                             false,
                             now)
        == probe_policy::decision::run);

  // A stamp that cannot be read is not freshness, and neither is one from the
  // future (a clock that moved back).
  CHECK(probe_policy::decision::run
        == probe_policy::decide(7400,
                                k_destination,
                                "not a timestamp",
                                k_destination,
                                k_encode_kbps,
                                false,
                                now));
  CHECK(probe_policy::decision::run
        == probe_policy::decide(7400,
                                k_destination,
                                stamp_local(now + 7200),
                                k_destination,
                                k_encode_kbps,
                                false,
                                now));
}

// The pre-start rule the encoder has always applied: the ramp rebuilds the
// sender, so it never runs under a live stream -- whatever the stored figure
// says.
TEST_CASE("a live stream still refuses the ramp", "[probe][policy]")
{
  const std::time_t now = 1'800'000'000;
  CHECK(probe_policy::decision::streaming
        == probe_policy::decide(0,
                                std::string {},
                                std::string {},
                                k_destination,
                                k_encode_kbps,
                                true,
                                now));
  CHECK(probe_policy::decision::streaming
        == probe_policy::decide(7400,
                                k_destination,
                                stamp_local(now),
                                k_destination,
                                k_encode_kbps,
                                true,
                                now));
}

// A failed probe must leave the session startable: the worker is idle again
// (not wedged on a dead thread), a later attempt may still measure, and the
// sender is sized from the conservative ceiling rather than a missing figure.
TEST_CASE("a failed probe leaves the session startable", "[probe][lifecycle]")
{
  probe_lifecycle::worker probe;
  std::atomic<int> attempts {0};

  // The ramp fails: it returns without producing a figure, as a refused or
  // aborted ramp does.
  REQUIRE(probe.start([&attempts] { ++attempts; }));
  // Wait for the body to have run and the worker to clear itself.
  for (int spin = 0; spin < 500 && probe.running(); ++spin) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(attempts.load() == 1);
  CHECK_FALSE(probe.running());

  // Not wedged: the next attempt runs.
  CHECK(probe.start([&attempts] { ++attempts; }));
  for (int spin = 0; spin < 500 && probe.running(); ++spin) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(attempts.load() == 2);
  CHECK_FALSE(probe.running());

  // With no figure stored the session still starts: nothing sizes the sender
  // from it, so the conservative payload-plus-headroom ceiling applies.
  const std::optional<int> measured =
      measured_upload_kbps_for(0, std::string {}, k_destination);
  CHECK_FALSE(measured.has_value());
  CHECK(rist_bandwidth_kbps(k_encode_kbps, measured, false)
        >= rist_stream_rate_kbps(k_encode_kbps));

  // And the ramp is still allowed to run for that link later.
  CHECK(probe_policy::decision::run
        == probe_policy::decide(0,
                                std::string {},
                                std::string {},
                                k_destination,
                                k_encode_kbps,
                                false,
                                1'800'000'000));
}
