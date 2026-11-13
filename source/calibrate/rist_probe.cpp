// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include "calibrate/rist_probe.h"

#include "lib/lib.h"
#include "lib/rist_bandwidth.h"
#include "transport/transport.h"

namespace calibrate
{

namespace
{

// libRIST carries ~1.3 kB per packet; transport::send_buffer splits on the same
// figure.
constexpr std::size_t k_packet_bytes = 1316;
// Pacing slice. A step is a few seconds long, so 20 ms is fine enough to hold
// the rate without spinning.
constexpr int k_tick_ms = 20;

// A fixed buffer of the size the leg carries. The bytes are not media: the ramp
// measures what the receiver acknowledges, not what a demuxer can parse.
auto probe_payload() -> const std::vector<std::uint8_t>&
{
  static const std::vector<std::uint8_t> payload = []
  {
    std::vector<std::uint8_t> buf(k_packet_bytes, 0);
    // MPEG-TS sync byte, so a capture of the ramp is recognisable as a test.
    buf.front() = 0x47;
    return buf;
  }();
  return payload;
}

}  // namespace

rist_stats_source::rist_stats_source(transport& sender)
    : sender_(&sender)
{
}

auto rist_stats_source::read(upload_probe::peer_sample& out) -> bool
{
  if (sender_ == nullptr) {
    return false;
  }
  transport::peer_snapshot snapshot;
  if (!sender_->latest_peer_snapshot(snapshot)) {
    return false;
  }
  if (have_epoch_ && snapshot.epoch == used_epoch_) {
    // libRIST reports once per interval: the sample already used says nothing
    // about the step that just ran.
    return false;
  }
  used_epoch_ = snapshot.epoch;
  have_epoch_ = true;
  out = peer_sample_from(sender_counters {
      .sent = snapshot.sent,
      .received = snapshot.received,
      .retransmitted = snapshot.retransmitted,
      .rtt_ms = snapshot.rtt_ms,
      .quality = snapshot.quality,
  });
  return true;
}

rist_payload_ramp::rist_payload_ramp(transport& sender)
    : sender_(&sender)
{
}

rist_payload_ramp::~rist_payload_ramp()
{
  stop();
}

auto rist_payload_ramp::start(int kbps) -> bool
{
  stop();
  if (sender_ == nullptr || kbps <= 0) {
    return false;
  }
  // One packet before the thread starts: a sender that refuses the write is a
  // leg that could not be opened at this rate, and the step is not worth
  // spending its dwell on.
  if (!sender_->send_buffer(probe_payload(), 0)) {
    return false;
  }
  kbps_.store(kbps, std::memory_order_relaxed);
  running_.store(true, std::memory_order_release);
  pacer_ = std::thread([this] { pace(); });
  return true;
}

void rist_payload_ramp::stop()
{
  running_.store(false, std::memory_order_release);
  if (pacer_.joinable()) {
    pacer_.join();
  }
}

void rist_payload_ramp::pace()
{
  using clock = std::chrono::steady_clock;
  const std::vector<std::uint8_t>& payload = probe_payload();
  auto next = clock::now();
  double owed = 0.0;
  while (running_.load(std::memory_order_acquire)) {
    owed += static_cast<double>(kbps_.load(std::memory_order_relaxed))
        * (1000.0 / 8.0) * static_cast<double>(k_tick_ms) / 1000.0;
    while (owed >= static_cast<double>(k_packet_bytes)) {
      if (!sender_->send_buffer(payload, 0)) {
        running_.store(false, std::memory_order_release);
        return;
      }
      owed -= static_cast<double>(k_packet_bytes);
    }
    next += std::chrono::milliseconds(k_tick_ms);
    std::this_thread::sleep_until(next);
    // A stalled tick is not repaid at full speed: the ramp states a rate, and
    // a burst after a stall would read as a spike the link never carried.
    if (clock::now() > next + std::chrono::milliseconds(k_tick_ms)) {
      next = clock::now();
      owed = 0.0;
    }
  }
}

auto run_probe(transport& sender, int encode_kbps, const upload_probe::hooks& h)
    -> upload_probe::outcome
{
  rist_stats_source stats(sender);
  rist_payload_ramp ramp(sender);
  // The ramp targets the rate the stream needs on the wire, not the video
  // bitrate alone: audio and the transport headers travel with it.
  const upload_probe::config cfg {
      .encode_kbps = rist_stream_rate_kbps(encode_kbps)};
  return upload_probe::run(cfg, stats, ramp, h);
}

}  // namespace calibrate
