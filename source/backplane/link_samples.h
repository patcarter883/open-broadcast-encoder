// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#ifndef OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_LINK_SAMPLES_H
#define OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_LINK_SAMPLES_H

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "backplane/backplane.h"
#include "stats/peer_table.h"

// Link telemetry from the encoded end (TELEMETRY_CONTRACT.md).
//
// POST /v1/sessions/{publicId}/link-samples takes one sample of the session's
// RIST flow. This producer is the SENDER: it counts what it put on the wire and
// what its peer reports back, and it cannot observe anything the receiving end
// counts. A field it cannot observe is sent as JSON null, never 0 -- a zero is
// an observation ("nothing was lost") that this end is in no position to make.
//
// The body builder is pure and the HTTP call is injected, matching
// bridge/reporting.h, so the wire shape is asserted rather than assumed.
namespace backplane
{

// The link figures the sender can observe. An empty optional is a field this
// end did not observe, and it is serialised as null.
struct link_reading
{
  std::optional<double> quality;  // percent, 0-100
  std::optional<std::uint32_t> rtt_ms;
  std::optional<std::uint64_t> bandwidth_bps;
  std::optional<std::uint64_t> retry_bandwidth_bps;
};

// The sender's view of a bond: summed bandwidth across legs, the worst leg's
// quality and RTT. An aggregate with no leg reporting yet yields an empty
// reading, so a session that has not carried a sample sends no figures.
link_reading link_reading_from(const peer_stats::aggregate& peers);

// The sample body, envelope included. `producer` is always "encoder";
// `sampled_at` is passed in so the value on the wire is asserted, not assumed.
nlohmann::json link_sample_body(const std::string& session_public_id,
                                std::uint64_t seq,
                                const std::string& sampled_at,
                                const link_reading& reading);

// ISO-8601 with the local UTC offset, e.g. "2026-11-13T13:05:02+11:00".
std::string iso8601_at(std::chrono::system_clock::time_point when);

struct link_sample_result
{
  bool ok = false;
  int http_status = 0;
  std::string error;
};

class link_sample_reporter
{
public:
  // transport: (method, path, bearer_token, body) -> (http_status, body).
  // Status 0 means the request never reached the server. The bearer is the
  // session's control token, so the base URL lives in the transport.
  using transport_fn = backplane_client::transport_fn;

  explicit link_sample_reporter(transport_fn transport = {});

  // POST one sample for `session_public_id`. `control_token` is that session's
  // credential -- the value retained from the allocation. It is show-once, so
  // it is passed in rather than fetched, and an empty one is refused without a
  // request rather than sent as an unauthenticated sample.
  link_sample_result report(const std::string& session_public_id,
                            const std::string& control_token,
                            const link_reading& reading,
                            std::chrono::system_clock::time_point sampled_at =
                                std::chrono::system_clock::now());

  // The seq of this session's next sample, from 0. Advances only when the
  // backplane accepted a sample: a failed POST is retried under the same seq,
  // which the backplane answers as a duplicate rather than a second row.
  std::uint64_t next_seq(const std::string& session_public_id) const;

private:
  transport_fn m_transport;
  std::map<std::string, std::uint64_t> m_seq;
};

}  // namespace backplane

// Build an httplib-backed transport for the sample endpoint. Defined in
// link_samples.cpp so this header stays free of httplib and links into the
// tests.
backplane::link_sample_reporter::transport_fn make_link_sample_transport(
    const std::string& base_url);

#endif  // OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_LINK_SAMPLES_H
