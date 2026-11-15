// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// End Session is one operator action: it closes the session and, if a feed is
// running, stops it. What matters and is pinned here is the ORDER -- the encode
// is stopped and joined before the probe is cancelled, and the session is
// deallocated only after both -- plus the two facts that keep it safe: the
// button is live whenever a session is held, and a stop that does not complete
// leaves the session held instead of deallocating under a live sender.
#include <string>
#include <vector>

#include "ui/end_session_flow.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
// The steps as they happen, in one vector, so the sequence is the assertion.
using steps = std::vector<std::string>;
}  // namespace

// A live feed no longer takes the action away: End Session stops the stream
// itself, so the button follows the session and nothing else.
TEST_CASE("End Session is enabled while streaming and only then by the session",
          "[end-session][ui]")
{
  using end_session_flow::enabled;
  using end_session_flow::state;

  // Held and streaming: this is the case the old rule refused.
  CHECK(enabled(state {.session_held = true, .encode_active = true}));
  // Held on the standby slate.
  CHECK(enabled(state {.session_held = true, .encode_active = false}));
  // No session: nothing to end, whether or not a stream is up.
  CHECK_FALSE(enabled(state {.session_held = false, .encode_active = true}));
  CHECK_FALSE(enabled(state {.session_held = false, .encode_active = false}));
}

// The order is the whole point: a sender that is still running must never have
// its session torn out from under it.
TEST_CASE("End Session stops the encode, then the probe, then deallocates",
          "[end-session][order]")
{
  steps seen;

  const auto outcome = end_session_flow::run(
      [&seen]
      {
        seen.emplace_back("stop-encode");
        return end_session_flow::stop_outcome::stopped;
      },
      [&seen]
      {
        seen.emplace_back("cancel-probe");
        return true;
      },
      [&seen] { seen.emplace_back("deallocate"); });

  CHECK(outcome.stop == end_session_flow::stop_outcome::stopped);
  CHECK(outcome.probe_cancelled);
  CHECK(outcome.deallocated);
  CHECK(seen == steps {"stop-encode", "cancel-probe", "deallocate"});

  // The assertion has teeth: the sequence this action must never produce -- the
  // deallocate ahead of the stop -- does not satisfy it. A run() that swapped
  // its steps therefore fails this test rather than passing on a list that
  // matches anything.
  const steps torn_down_first {"deallocate", "cancel-probe", "stop-encode"};
  CHECK_FALSE(torn_down_first
              == steps {"stop-encode", "cancel-probe", "deallocate"});
}

// A stop that did not complete is a refusal, not a partial teardown: the sender
// may still be writing, so neither the probe nor the session is touched.
TEST_CASE("A failed or timed-out stop leaves the session held",
          "[end-session][order]")
{
  using end_session_flow::stop_completed;
  using end_session_flow::stop_outcome;

  for (const auto failure : {stop_outcome::timed_out, stop_outcome::failed}) {
    steps seen;
    const auto outcome = end_session_flow::run(
        [&seen, failure]
        {
          seen.emplace_back("stop-encode");
          return failure;
        },
        [&seen]
        {
          seen.emplace_back("cancel-probe");
          return true;
        },
        [&seen] { seen.emplace_back("deallocate"); });

    CHECK_FALSE(stop_completed(outcome.stop));
    CHECK_FALSE(outcome.deallocated);
    CHECK_FALSE(outcome.probe_cancelled);
    // Nothing downstream of the failed stop ran at all.
    CHECK(seen == steps {"stop-encode"});
    // And the operator is told why, rather than left with a silent no-op.
    CHECK(std::string {end_session_flow::stop_failure_text(outcome.stop)}.size()
          > 0);
  }

  // A completed stop, in both of its forms, is not a refusal.
  CHECK(stop_completed(stop_outcome::stopped));
  CHECK(stop_completed(stop_outcome::nothing_running));
  CHECK(std::string {end_session_flow::stop_failure_text(stop_outcome::stopped)}
            .empty());
}

// One stray click on a live broadcast must not end the session, so the guard
// stays: the first click only arms, and only a second click inside the window
// confirms.
TEST_CASE("End Session still needs a second click inside the window",
          "[end-session][confirm]")
{
  using answer = end_session_flow::confirm::answer;
  end_session_flow::confirm confirm;

  // First click arms it and runs nothing.
  CHECK(confirm.click(100.0) == answer::armed);
  CHECK(confirm.armed());

  // Second click inside the window confirms, and disarms.
  const double inside = 100.0 + end_session_flow::arm_window_seconds - 0.1;
  CHECK(confirm.click(inside) == answer::confirmed);
  CHECK_FALSE(confirm.armed());

  // A third click is a fresh arm, not a third confirmation.
  CHECK(confirm.click(inside + 1.0) == answer::armed);

  // A click after the window has run out re-arms instead of confirming: the
  // arm the operator took has lapsed.
  end_session_flow::confirm lapsed;
  CHECK(lapsed.click(10.0) == answer::armed);
  const double after = 10.0 + end_session_flow::arm_window_seconds + 0.1;
  CHECK(lapsed.click(after) == answer::armed);
  CHECK(lapsed.armed());

  // The window timer's job: disarm so the label reverts.
  lapsed.disarm();
  CHECK_FALSE(lapsed.armed());
}
