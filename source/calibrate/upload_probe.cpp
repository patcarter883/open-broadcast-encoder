// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <chrono>
#include <thread>

#include "calibrate/upload_probe.h"

namespace upload_probe
{

namespace
{

// Slice of a step's dwell, so an abort is noticed promptly instead of after the
// whole step.
constexpr int k_poll_slice_ms = 50;

}  // namespace

auto plan(const config& cfg) -> std::vector<step_plan>
{
  std::vector<step_plan> steps;
  if (cfg.encode_kbps <= 0) {
    return steps;
  }
  const int step_ms = std::max(cfg.step_ms, 0);
  const int max_total_ms = std::max(cfg.max_total_ms, 0);
  for (const int percent : k_step_percent) {
    if (!steps.empty()
        && (static_cast<int>(steps.size()) + 1) * step_ms > max_total_ms)
    {
      break;
    }
    step_plan step;
    step.percent = percent;
    // Never below 1 kbps: a zero-rate step drives no payload and would read as
    // clean for the wrong reason.
    step.kbps = std::max(cfg.encode_kbps * percent / 100, 1);
    steps.push_back(step);
  }
  return steps;
}

auto judge(const peer_sample& sample) -> verdict
{
  if (!sample.valid) {
    return verdict::unknown;
  }
  if (!sample.counters_valid) {
    return sample.quality >= k_quality_floor ? verdict::clean : verdict::lossy;
  }
  if (sample.sent == 0) {
    return verdict::unknown;
  }
  if (sample.lost > 0) {
    return verdict::lossy;
  }
  if (sample.missing > sample.recovered) {
    return verdict::lossy;
  }
  return verdict::clean;
}

auto select(const std::vector<step_plan>& planned,
            const std::vector<step_result>& run,
            bool aborted) -> outcome
{
  outcome out;
  out.steps = run;
  if (planned.empty()) {
    return out;
  }
  const bool stopped_clean = run.empty() || run.back().seen == verdict::clean;
  if (aborted || (run.size() < planned.size() && stopped_clean)) {
    out.aborted = true;
    return out;
  }

  for (const step_result& step : run) {
    if (step.seen != verdict::clean) {
      // The first step already failed: the leg carries less than the encode
      // rate and there is no figure to report.
      return out;
    }
    out.ok = true;
    out.upload_kbps = step.kbps;
    out.percent = step.percent;
    out.by_quality = !step.stats.counters_valid;
  }
  return out;
}

auto run(const config& cfg,
         stats_source& stats,
         payload_ramp& ramp,
         const hooks& h) -> outcome
{
  const std::vector<step_plan> planned = plan(cfg);
  std::vector<step_result> steps;
  steps.reserve(planned.size());

  const auto keep_running = [&h]()
  { return !h.keep_running || h.keep_running(); };
  const int step_ms = std::max(cfg.step_ms, 0);

  const auto dwell = [&h, &keep_running, step_ms]() -> bool
  {
    if (h.sleep_ms) {
      h.sleep_ms(step_ms);
      return keep_running();
    }
    int waited = 0;
    while (waited < step_ms) {
      if (!keep_running()) {
        return false;
      }
      const int slice = std::min(k_poll_slice_ms, step_ms - waited);
      std::this_thread::sleep_for(std::chrono::milliseconds(slice));
      waited += slice;
    }
    return keep_running();
  };

  bool aborted = false;
  for (const step_plan& step : planned) {
    if (!keep_running() || !ramp.start(step.kbps)) {
      aborted = true;
      break;
    }
    const bool ran = dwell();
    ramp.stop();
    if (!ran) {
      aborted = true;
      break;
    }
    step_result result;
    result.percent = step.percent;
    result.kbps = step.kbps;
    result.stats.valid = stats.read(result.stats);
    result.seen = judge(result.stats);
    steps.push_back(result);
    if (result.seen != verdict::clean) {
      break;
    }
  }

  return select(planned, steps, aborted);
}

}  // namespace upload_probe
