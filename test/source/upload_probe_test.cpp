// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The direct-mode upload probe judged on stubbed RIST peer statistics: the
// selection rule, the quality fallback and the abort path, with no libRIST, no
// GStreamer and no socket.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "calibrate/upload_probe.h"

#include <catch2/catch_test_macros.hpp>

namespace
{

using upload_probe::peer_sample;
using upload_probe::step_result;
using upload_probe::verdict;

// A leg carrying the step's payload with nothing lost: the counters say the
// packets went first attempt.
auto clean_sample(std::uint64_t sent = 3000) -> peer_sample
{
  peer_sample s;
  s.valid = true;
  s.counters_valid = true;
  s.sent = sent;
  s.received = sent;
  s.quality = 100.0;
  s.rtt_ms = 40;
  return s;
}

// The negative-budget symptom: packets are going missing and nothing is being
// retransmitted, so `recovered` stays at zero while `lost` climbs.
auto broken_sample() -> peer_sample
{
  peer_sample s = clean_sample();
  s.missing = 120;
  s.recovered = 0;
  s.lost = 120;
  s.received = s.sent - s.missing;
  s.quality = 96.0;
  return s;
}

// A source that reports quality only -- no packet counters.
auto quality_sample(double quality) -> peer_sample
{
  peer_sample s;
  s.valid = true;
  s.counters_valid = false;
  s.quality = quality;
  return s;
}

auto encode_config(int kbps) -> upload_probe::config
{
  upload_probe::config cfg;
  cfg.encode_kbps = kbps;
  return cfg;
}

// Drives run() against a fixed list of per-step samples, with a dwell that does
// not sleep.
class scripted_stats : public upload_probe::stats_source
{
public:
  explicit scripted_stats(std::vector<peer_sample> samples)
      : samples_(std::move(samples))
  {
  }

  auto read(peer_sample& out) -> bool override
  {
    if (next_ >= samples_.size()) {
      out = peer_sample {};
      return false;
    }
    out = samples_[next_++];
    return out.valid;
  }

  auto reads() const -> std::size_t { return next_; }

private:
  std::vector<peer_sample> samples_;
  std::size_t next_ = 0;
};

class counting_ramp : public upload_probe::payload_ramp
{
public:
  auto start(int kbps) -> bool override
  {
    starts.push_back(kbps);
    running = true;
    return true;
  }

  void stop() override
  {
    running = false;
    ++stops;
  }

  std::vector<int> starts;
  int stops = 0;
  bool running = false;
};

auto instant() -> upload_probe::hooks
{
  upload_probe::hooks h;
  h.sleep_ms = [](int) {};
  return h;
}

}  // namespace

TEST_CASE("the ramp asks only for the rate the stream needs",
          "[calibrate][upload]")
{
  const auto steps = upload_probe::plan(encode_config(30000));
  REQUIRE(steps.size() == 1);
  REQUIRE(steps[0].percent == 100);
  REQUIRE(steps[0].kbps == 30000);

  // A dwell long enough to overrun the cap still plans the one step it has.
  upload_probe::config slow = encode_config(30000);
  slow.step_ms = 2000;
  REQUIRE(upload_probe::plan(slow).size() == 1);

  // No rate, no ramp: there is nothing to scale.
  REQUIRE(upload_probe::plan(encode_config(0)).empty());
}

TEST_CASE("a clean ramp returns the rate it probed", "[calibrate][upload]")
{
  counting_ramp ramp;
  scripted_stats stats({clean_sample()});
  const auto out =
      upload_probe::run(encode_config(30000), stats, ramp, instant());

  REQUIRE(out.ok);
  REQUIRE_FALSE(out.aborted);
  REQUIRE(out.upload_kbps == 30000);
  REQUIRE(out.percent == 100);
  REQUIRE_FALSE(out.by_quality);
  REQUIRE(out.steps.size() == 1);
  REQUIRE(ramp.starts.size() == 1);
  REQUIRE(ramp.stops == 1);
  REQUIRE_FALSE(ramp.running);
}

TEST_CASE("loss at the rate the stream needs yields no figure",
          "[calibrate][upload]")
{
  counting_ramp ramp;
  scripted_stats stats({broken_sample()});
  const auto out =
      upload_probe::run(encode_config(30000), stats, ramp, instant());

  // The leg could not carry the stream: there is no headroom to report.
  REQUIRE_FALSE(out.ok);
  REQUIRE(out.upload_kbps == 0);
  REQUIRE(out.steps.size() == 1);
  REQUIRE(ramp.starts.size() == 1);
  REQUIRE(stats.reads() == 1);
  REQUIRE(out.steps.back().seen == verdict::lossy);
}

TEST_CASE("missing packets that were all recovered stay clean",
          "[calibrate][upload]")
{
  peer_sample recovered = clean_sample();
  recovered.missing = 40;
  recovered.recovered = 40;
  recovered.received = recovered.sent;
  recovered.quality = 99.4;

  REQUIRE(upload_probe::judge(recovered) == verdict::clean);
  REQUIRE(upload_probe::judge(broken_sample()) == verdict::lossy);
  REQUIRE(upload_probe::judge(peer_sample {}) == verdict::unknown);

  // A step that carried nothing proves nothing, whatever its counters say.
  peer_sample empty = clean_sample(0);
  empty.received = 0;
  REQUIRE(upload_probe::judge(empty) == verdict::unknown);
}

TEST_CASE("with no loss counters the quality rule decides",
          "[calibrate][upload]")
{
  counting_ramp ramp;
  scripted_stats stats({quality_sample(100.0)});
  const auto out =
      upload_probe::run(encode_config(30000), stats, ramp, instant());

  REQUIRE(out.ok);
  REQUIRE(out.by_quality);
  REQUIRE(out.percent == 100);
  REQUIRE(out.upload_kbps == 30000);

  // Recovery is all-or-nothing: anything under a full 100 is losing frames.
  REQUIRE(upload_probe::judge(quality_sample(100.0)) == verdict::clean);
  REQUIRE(upload_probe::judge(quality_sample(99.9)) == verdict::lossy);
}

TEST_CASE("a leg that reports nothing yields no figure", "[calibrate][upload]")
{
  counting_ramp ramp;
  scripted_stats stats({});
  const auto out =
      upload_probe::run(encode_config(30000), stats, ramp, instant());

  REQUIRE_FALSE(out.ok);
  REQUIRE_FALSE(out.aborted);
  REQUIRE(out.upload_kbps == 0);
  REQUIRE(out.steps.size() == 1);
  REQUIRE(out.steps.front().seen == verdict::unknown);
}

TEST_CASE("a probe aborted mid-ramp returns no result", "[calibrate][upload]")
{
  counting_ramp ramp;
  scripted_stats stats(
      {clean_sample(), clean_sample(), clean_sample(), clean_sample()});
  upload_probe::hooks h = instant();
  // A stream starting mid-probe: the second poll says give way.
  int polls = 0;
  h.keep_running = [&polls]() { return ++polls <= 1; };

  const auto out = upload_probe::run(encode_config(30000), stats, ramp, h);

  REQUIRE(out.aborted);
  REQUIRE_FALSE(out.ok);
  REQUIRE(out.upload_kbps == 0);
  REQUIRE(out.percent == 0);
  // Aborted inside the step, and the leg was released rather than left running.
  REQUIRE(out.steps.empty());
  REQUIRE(ramp.stops == 1);
  REQUIRE_FALSE(ramp.running);
}

TEST_CASE("a short ramp is never selected as a figure", "[calibrate][upload]")
{
  const auto steps = upload_probe::plan(encode_config(30000));
  // A run that stopped short while still clean was aborted, not measured.
  std::vector<step_result> partial;

  const auto out = upload_probe::select(steps, partial);
  REQUIRE(out.aborted);
  REQUIRE_FALSE(out.ok);
  REQUIRE(out.upload_kbps == 0);

  // A lossy first step carries less than the encode rate: no figure at all.
  std::vector<step_result> failing(1);
  failing[0].seen = verdict::lossy;
  const auto none = upload_probe::select(steps, failing);
  REQUIRE_FALSE(none.ok);
  REQUIRE(none.upload_kbps == 0);
}

TEST_CASE("the real dwell polls for an abort instead of sleeping it out",
          "[calibrate][upload]")
{
  counting_ramp ramp;
  scripted_stats stats(
      {clean_sample(), clean_sample(), clean_sample(), clean_sample()});

  upload_probe::config fast = encode_config(30000);
  fast.step_ms = 5;
  fast.settle_ms = 5;
  // No sleep hook: the runner's own slice loop is exercised.
  const auto out = upload_probe::run(fast, stats, ramp, upload_probe::hooks {});

  REQUIRE(out.ok);
  REQUIRE(out.upload_kbps == 30000);
  REQUIRE(out.steps.size() == 1);
  REQUIRE_FALSE(ramp.running);
}
