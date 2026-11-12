// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Folds librist's per-packet send failures into one line per second.
//
// When a bonded leg's socket stops accepting (interface down, route gone),
// librist reports every packet it cannot send: two lines per packet, at the
// packet rate. At 6 Mbit/s that is roughly 1,100 lines a second, which buries
// the transport pane exactly when an operator is looking at it to find out why
// the picture degraded.
//
// Equality-based collapsing is not enough here. The first line of the pair is
// byte-identical and folds; the second carries the packet's sequence number and
// never repeats. So the failures are counted and summarised on a time window
// instead of matched against the previous line.
//
// Everything that is not a per-send failure passes through untouched: this is a
// filter for one noisy class of message, not a quietening of librist.
class librist_log_filter
{
public:
  // Lines to forward, in order. A per-send failure contributes nothing except
  // the window summary when one falls due, and a burst that ends emits one
  // closing line with the totals.
  auto on_line(const std::string& msg, std::int64_t now_ms)
      -> std::vector<std::string>;

private:
  static auto is_send_failure(const std::string& msg) -> bool;
  static auto errno_tag(const std::string& msg) -> std::string;
  auto summary(std::int64_t now_ms, bool closing) -> std::string;

  std::mutex mutex;
  // One window being counted, and the last time a summary went out. `in_burst`
  // is an explicit flag rather than a zero sentinel: the caller may
  // legitimately pass 0 as "now".
  bool in_burst {false};
  std::int64_t last_failure_ms {0};
  std::int64_t window_start_ms {0};
  std::int64_t last_summary_ms {0};
  std::uint64_t window_failures {0};
  // Totals for the burst, so the closing line says how bad it was.
  std::uint64_t burst_failures {0};
  std::int64_t burst_start_ms {0};
  std::string last_errno_tag;
};
