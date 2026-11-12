// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <cstdint>
#include <format>
#include <utility>

#include "transport/librist_log_filter.h"

namespace
{
// librist's two per-send lines. The sendto one carries a sequence number, so it
// never repeats byte-for-byte -- which is why counting beats matching.
constexpr const char* kSendFailed = "Send failed via";
constexpr const char* kSendtoFailed = "Error on transmission sendto";

// One summary a second: readable at a glance, and still fast enough to show the
// leg flapping rather than sitting flat.
constexpr std::int64_t kSummaryIntervalMs = 1000;

// Failures must be absent this long before the burst is called over. A live
// bond keeps logging from the surviving leg, so anything shorter reads ordinary
// traffic as a recovery.
constexpr std::int64_t kQuietPeriodMs = 2000;

// The errno names an operator recognises. Anything else is printed as a number
// rather than guessed at.
auto errno_name(const std::string& tag) -> std::string
{
  if (tag == "errno=101") {
    return "Network is unreachable";
  }
  if (tag == "errno=111") {
    return "Connection refused";
  }
  if (tag == "errno=113") {
    return "No route to host";
  }
  if (tag == "errno=9") {
    return "Bad file descriptor";
  }
  if (tag == "errno=90") {
    return "Message too long";
  }
  return {};
}
}  // namespace

auto librist_log_filter::is_send_failure(const std::string& msg) -> bool
{
  return msg.find(kSendFailed) != std::string::npos
      || msg.find(kSendtoFailed) != std::string::npos;
}

// "errno=101" out of "Send failed via main-profile sendmsg: errno=101,
// size=1340"
auto librist_log_filter::errno_tag(const std::string& msg) -> std::string
{
  const auto at = msg.find("errno=");
  if (at == std::string::npos) {
    return {};
  }
  const auto end = msg.find_first_of(", \t\n", at);
  return msg.substr(at, end == std::string::npos ? end : end - at);
}

auto librist_log_filter::summary(std::int64_t now_ms, bool closing)
    -> std::string
{
  const std::int64_t span_ms = closing ? (now_ms - this->burst_start_ms)
                                       : (now_ms - this->window_start_ms);
  const std::uint64_t count =
      closing ? this->burst_failures : this->window_failures;

  const std::string name = errno_name(this->last_errno_tag);
  const std::string cause = this->last_errno_tag.empty()
      ? std::string {"no errno reported"}
      : (name.empty() ? this->last_errno_tag
                      : this->last_errno_tag + ": " + name);

  if (closing) {
    return std::format(
        "[rist] send failures stopped after {} packet(s) over {:.1f}s ({}) "
        "- the leg is carrying again\n",
        count,
        static_cast<double>(span_ms) / 1000.0,
        cause);
  }

  return std::format(
      "[rist] {} send failure(s) in the last {:.1f}s ({}) - media is still "
      "crossing on the remaining legs\n",
      count,
      static_cast<double>(span_ms) / 1000.0,
      cause);
}

auto librist_log_filter::on_line(const std::string& msg, std::int64_t now_ms)
    -> std::vector<std::string>
{
  const std::lock_guard<std::mutex> guard(this->mutex);
  std::vector<std::string> out;

  if (!is_send_failure(msg)) {
    // "Recovered" is a claim about the link, so it may only be made after the
    // failures have actually stopped. A live bond keeps logging from the
    // surviving leg, so a merely different line is not evidence of recovery:
    // closing on that produced a "the leg is carrying again" every few lines
    // while it was still dead.
    if (this->in_burst && now_ms - this->last_failure_ms >= kQuietPeriodMs) {
      out.push_back(this->summary(now_ms, true));
      this->in_burst = false;
      this->burst_failures = 0;
      this->burst_start_ms = 0;
      this->window_failures = 0;
      this->window_start_ms = 0;
      this->last_summary_ms = 0;
    }
    out.push_back(msg);
    return out;
  }

  const std::string tag = errno_tag(msg);
  if (!tag.empty()) {
    this->last_errno_tag = tag;
  }

  if (!this->in_burst) {
    this->in_burst = true;
    this->window_start_ms = now_ms;
    this->burst_start_ms = now_ms;
    this->last_summary_ms = now_ms;
    this->window_failures = 0;
    this->burst_failures = 0;
  }
  this->last_failure_ms = now_ms;
  ++this->window_failures;
  ++this->burst_failures;

  if (now_ms - this->last_summary_ms >= kSummaryIntervalMs) {
    out.push_back(this->summary(now_ms, false));
    this->window_start_ms = now_ms;
    this->last_summary_ms = now_ms;
    this->window_failures = 0;
  }
  return out;
}
