// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The direct-mode probe's two ends, wired to the encoder's own RIST sender:
// the stats source reads the sender-peer samples libRIST already reports, and
// the payload ramp writes through that same sender. The mapping and the sizing
// rules are free functions so they compile and are pinned with no libRIST.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <string>
#include <thread>

#include "calibrate/upload_probe.h"
#include "lib/rist_bandwidth.h"

class transport;

namespace calibrate
{

// The sender-peer counters libRIST reports for the leg, in rist.h's spelling.
struct sender_counters
{
  std::uint64_t sent = 0;
  std::uint64_t received = 0;
  std::uint64_t retransmitted = 0;
  std::uint32_t rtt_ms = 0;
  double quality = 0.0;
};

// A sender peer reports sent/received/retransmitted, quality and RTT -- the
// loss counters (missing/recovered/lost) are receiver-flow fields. A step is
// therefore judged on quality, which libRIST computes from the retransmissions
// and the packets it had to skip: a leg whose ceiling sits under the payload
// reads below 100 even while the counters still look clean.
inline auto peer_sample_from(const sender_counters& counters)
    -> upload_probe::peer_sample
{
  upload_probe::peer_sample sample;
  sample.valid = true;
  // No loss counters on a sender peer: the rule falls back to quality.
  sample.counters_valid = false;
  sample.sent = counters.sent;
  sample.received = counters.received;
  sample.rtt_ms = counters.rtt_ms;
  // libRIST's quality counts what it sent and what it had to skip; it says
  // nothing about a peer that never answered. Without a single acknowledged
  // packet the ramp has proven no delivery, whatever the send-side counters
  // read, so the step is reported as bad rather than clean.
  sample.quality = counters.received == 0 ? 0.0 : counters.quality;
  return sample;
}

// The bandwidth the probe sender declares: the ramp's top step plus the same
// retransmission headroom a direct session funds, so the ramp is testing the
// link rather than libRIST's own ceiling.
inline auto probe_bandwidth_kbps(int encode_kbps) -> int
{
  const upload_probe::config cfg {.encode_kbps = encode_kbps};
  int top = std::max(encode_kbps, 0);
  for (const upload_probe::step_plan& step : upload_probe::plan(cfg)) {
    top = std::max(top, step.kbps);
  }
  // transport::setup_rist_sender clamps anything under 100, and a probe of a
  // link below 100 kbps has nothing to say.
  return std::max(top + rist_direct_headroom_kbps, 100);
}

// What the operator is shown after a ramp.
inline auto format_outcome(const upload_probe::outcome& outcome) -> std::string
{
  if (outcome.ok) {
    return std::format("{} kbps ({}% of the encode rate)",
                       outcome.upload_kbps,
                       outcome.percent);
  }
  if (outcome.aborted) {
    return "not measured - the ramp stopped early";
  }
  return "no figure - the leg did not carry the first step";
}

// The stats source over a live sender.
class rist_stats_source final : public upload_probe::stats_source
{
public:
  explicit rist_stats_source(transport& sender);
  // True only when a sample newer than the previous read has arrived, so a
  // step with no report of its own reads as unknown rather than as clean.
  auto read(upload_probe::peer_sample& out) -> bool override;

private:
  transport* sender_ = nullptr;
  std::uint64_t used_epoch_ = 0;
  bool have_epoch_ = false;
};

// The payload driver: writes synthetic packets at the requested rate through
// the sender the caller built. The payload is not media -- libRIST counts what
// the receiver acknowledges, which is what the ramp is measuring.
class rist_payload_ramp final : public upload_probe::payload_ramp
{
public:
  explicit rist_payload_ramp(transport& sender);
  ~rist_payload_ramp() override;
  rist_payload_ramp(const rist_payload_ramp&) = delete;
  rist_payload_ramp& operator=(const rist_payload_ramp&) = delete;

  auto start(int kbps) -> bool override;
  void stop() override;

private:
  void pace();

  transport* sender_ = nullptr;
  std::atomic<bool> running_ {false};
  std::atomic<int> kbps_ {0};
  std::thread pacer_;
};

// Runs the whole ramp against a sender the caller has already built and
// configured. BLOCKING for the length of the ramp.
auto run_probe(transport& sender,
               int encode_kbps,
               const upload_probe::hooks& h = {}) -> upload_probe::outcome;

}  // namespace calibrate
