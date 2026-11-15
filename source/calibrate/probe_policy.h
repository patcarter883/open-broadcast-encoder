// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// When the upload ramp is allowed to run.
//
// The ramp exists to size a DIRECT sender, and it writes test traffic to the
// session's own destination. Running it on every allocation therefore adds load
// to a node that may already be at its ceiling, and on a capacity run it adds
// that load concurrently from every encoder. The measurement is a property of
// the link, not of the allocation, so it is REUSED: the ramp runs only when
// there is no figure for this destination, or the figure is older than the
// window below, or the operator asked for it with Calibrate upload.
//
// The rules are pure functions of the stored figure, the destination and the
// clock, so they are pinned by a test with no sender, no settings file and no
// portal.
#pragma once

#include <cstdint>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace probe_policy
{

// How long a measured figure is trusted before the ramp runs again for it.
// Twelve hours covers a broadcast day: an operator re-allocating between venues,
// rehearsals and the show reuses one measurement, while the next day's link is
// measured again rather than assumed unchanged.
inline constexpr std::int64_t k_fresh_for_seconds = 12 * 60 * 60;

enum class decision
{
  // No usable figure for this destination: run the ramp.
  run,
  // A figure for this destination, inside the window: reuse it.
  fresh,
  // Nothing to measure against -- the operator sets RIST address first.
  no_destination,
  // The ramp's plan scales from the encode rate; without it there is no ramp.
  no_bitrate,
  // A live sender owns the transporter the ramp would rebuild.
  streaming,
};

// The stored stamp, "2026-11-13 14:32" in LOCAL time (settings' format). An
// empty or malformed stamp yields nothing: a figure with no record of when it
// was taken cannot be aged, so it is never treated as fresh.
inline auto parse_taken_at(std::string_view text) -> std::optional<std::time_t>
{
  if (text.empty()) {
    return std::nullopt;
  }
  std::tm stamp {};
  std::istringstream in {std::string {text}};
  in >> std::get_time(&stamp, "%Y-%m-%d %H:%M");
  if (in.fail()) {
    return std::nullopt;
  }
  // The file holds local time, so it is read back as local time. -1 asks
  // mktime() to decide DST for that date rather than trusting tm_isdst.
  stamp.tm_isdst = -1;
  const std::time_t taken = std::mktime(&stamp);
  if (taken == static_cast<std::time_t>(-1)) {
    return std::nullopt;
  }
  return taken;
}

// Seconds between the stamp and `now`. Negative when the stamp is in the
// future, which a moved clock produces; the caller treats that as stale.
inline auto age_seconds(std::string_view taken_at, std::time_t now)
    -> std::optional<std::int64_t>
{
  const std::optional<std::time_t> taken = parse_taken_at(taken_at);
  if (!taken.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(now)
      - static_cast<std::int64_t>(*taken);
}

// Should this allocation run the ramp?
//
// `streaming` is the safety rule and is checked first: the ramp rebuilds the
// transporter, so it must never run under a live sender, whatever the stored
// figure says. A figure is reused only when it describes THIS destination --
// the same rule rist_bandwidth_kbps() applies when it sizes the sender -- and
// only while it is inside k_fresh_for_seconds.
inline auto decide(int measured_kbps,
                   std::string_view measured_destination,
                   std::string_view taken_at,
                   std::string_view destination,
                   int encode_kbps,
                   bool streaming,
                   std::time_t now) -> decision
{
  if (streaming) {
    return decision::streaming;
  }
  if (destination.empty()) {
    return decision::no_destination;
  }
  if (encode_kbps <= 0) {
    return decision::no_bitrate;
  }
  if (measured_kbps <= 0 || measured_destination != destination) {
    return decision::run;
  }
  const std::optional<std::int64_t> age = age_seconds(taken_at, now);
  if (!age.has_value() || *age < 0 || *age > k_fresh_for_seconds) {
    return decision::run;
  }
  return decision::fresh;
}

// The operator-facing reason, for the transport log. A skipped ramp is stated
// so the absence of probe traffic is a decision on the record rather than
// something the operator has to infer.
inline auto describe(decision shown) -> std::string_view
{
  switch (shown) {
    case decision::run:
      return "measuring the uplink";
    case decision::fresh:
      return "reusing the stored measurement";
    case decision::no_destination:
      return "no RIST address to measure";
    case decision::no_bitrate:
      return "no encode bitrate to scale the ramp from";
    case decision::streaming:
      return "the stream is up: no ramp";
  }
  return "measuring the uplink";
}

}  // namespace probe_policy
