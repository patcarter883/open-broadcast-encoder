// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <string>
#include <tuple>
#include <utility>

#include "backplane/link_samples.h"

#include "backplane/device_auth.h"

namespace backplane
{
namespace
{
constexpr const char* k_producer = "encoder";

// Route lives under /api, where the backplane mounts routes/api.php. The
// credential is the session's own control token and the session is named in the
// path, so a write can only reach the session that issued it.
std::string samples_path(const std::string& session_public_id)
{
  return "/api/v1/sessions/" + session_public_id + "/link-samples";
}

// Every link field. The ones the sender cannot observe are left null, and the
// ones it can are filled from the reading below.
constexpr std::array<const char*, 14> k_link_fields {
    "quality",
    "rtt_ms",
    "received_window",
    "missing",
    "recovered_window",
    "recovered_one_retry_window",
    "lost_window",
    "reordered",
    "received_total",
    "recovered_total",
    "recovered_one_retry_total",
    "lost_total",
    "bandwidth_bps",
    "retry_bandwidth_bps",
};
}  // namespace

link_reading link_reading_from(const peer_stats::aggregate& peers)
{
  // No leg has reported, so this end has observed nothing. Every field stays
  // empty and is serialised as null.
  if (!peers.any) {
    return {};
  }

  link_reading reading;
  reading.quality = peers.quality;
  reading.rtt_ms = peers.rtt_ms;
  reading.bandwidth_bps = peers.bandwidth;
  reading.retry_bandwidth_bps = peers.retry_bandwidth;
  return reading;
}

nlohmann::json link_sample_body(const std::string& session_public_id,
                                std::uint64_t seq,
                                const std::string& sampled_at,
                                const link_reading& reading)
{
  nlohmann::json body {
      {"session_public_id", session_public_id},
      {"producer", k_producer},
      {"seq", seq},
      {"sampled_at", sampled_at},
  };

  for (const char* field : k_link_fields) {
    body[field] = nullptr;
  }

  // The sender's own figures. Absent ones stay null: the receiver's counters
  // are not this end's to report.
  if (reading.quality) {
    body["quality"] = *reading.quality;
  }
  if (reading.rtt_ms) {
    body["rtt_ms"] = *reading.rtt_ms;
  }
  if (reading.bandwidth_bps) {
    body["bandwidth_bps"] = *reading.bandwidth_bps;
  }
  if (reading.retry_bandwidth_bps) {
    body["retry_bandwidth_bps"] = *reading.retry_bandwidth_bps;
  }

  return body;
}

std::string iso8601_at(std::chrono::system_clock::time_point when)
{
  const std::time_t seconds = std::chrono::system_clock::to_time_t(when);
  std::tm local {};
#ifdef _WIN32
  if (localtime_s(&local, &seconds) != 0) {
    return {};
  }
#else
  if (localtime_r(&seconds, &local) == nullptr) {
    return {};
  }
#endif

  char stamp[32] {};
  if (std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S%z", &local) == 0) {
    return {};
  }

  std::string text = stamp;
  // %z is "+1100"; the contract asks for an offset with a colon.
  if (text.size() >= 5) {
    text.insert(text.size() - 2, ":");
  }
  return text;
}

link_sample_reporter::link_sample_reporter(transport_fn transport)
    : m_transport(std::move(transport))
{
}

std::uint64_t link_sample_reporter::next_seq(
    const std::string& session_public_id) const
{
  const auto it = m_seq.find(session_public_id);
  return it == m_seq.end() ? 0 : it->second;
}

link_sample_result link_sample_reporter::report(
    const std::string& session_public_id,
    const std::string& control_token,
    const link_reading& reading,
    std::chrono::system_clock::time_point sampled_at)
{
  link_sample_result result;

  if (!m_transport) {
    result.error = "no transport is configured";
    return result;
  }
  if (session_public_id.empty()) {
    result.error = "no session to report for";
    return result;
  }
  if (control_token.empty()) {
    // Show-once, and the device token authenticates the device rather than the
    // session. Without the session's own token there is nothing to authenticate
    // a sample with, and an unauthenticated POST would only be a 401.
    result.error = "no control token is held for this session";
    return result;
  }

  const std::uint64_t seq = next_seq(session_public_id);
  const std::string body =
      link_sample_body(session_public_id, seq, iso8601_at(sampled_at), reading)
          .dump();

  int status = 0;
  std::string response;
  try {
    std::tie(status, response) = m_transport(
        "POST", samples_path(session_public_id), control_token, body);
  } catch (const std::exception& e) {
    // A sample is a convenience, not the feed: a transport that throws must not
    // take the encoder down with it.
    result.error =
        std::string("the link sample could not be sent: ") + e.what();
    return result;
  }
  result.http_status = status;

  if (status == 0) {
    result.error = "no response from the backplane";
    return result;
  }

  if (status < 200 || status > 299) {
    // The backplane names the reason in `message`; preferring it over the bare
    // status is what tells an operator the token was refused for this session.
    nlohmann::json parsed = nlohmann::json::parse(response, nullptr, false);
    result.error = parsed.is_object()
        ? parsed.value("message",
                       std::string("the backplane returned HTTP ")
                           + std::to_string(status))
        : std::string("the backplane returned HTTP ") + std::to_string(status);
    return result;
  }

  // Only an accepted sample consumes the number.
  m_seq[session_public_id] = seq + 1;
  result.ok = true;
  return result;
}

}  // namespace backplane

// One httplib transport serves every bearer-authenticated POST on the
// backplane, so the sample endpoint reuses it rather than opening a second
// stack.
backplane::link_sample_reporter::transport_fn make_link_sample_transport(
    const std::string& base_url)
{
  return make_device_auth_transport(base_url);
}
