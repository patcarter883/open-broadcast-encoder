// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The upload probe for DIRECT mode. The encoder's RIST leg goes straight to the
// relay over the WAN, so there is no bridge to measure and no shaper to set:
// the only figure direct mode needs is how much this host can push before the
// leg starts dropping packets.
//
// The probe walks a short payload ramp and judges each step on DELIVERY, never
// on send rate. libRIST reports the leg's received/missing/recovered/lost
// counters once per second to the sender, so the answer comes from statistics
// the encoder already receives; nothing is added to the far side.
//
// Both sides of the ramp are injected (the statistics source and the payload
// driver), so the selection rule compiles and runs with no libRIST, no
// GStreamer and no network.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace upload_probe
{

// One per-interval sample of the leg the sender is writing to, in the encoding
// libRIST uses for rist_stats_sender_peer.
struct peer_sample
{
  // The source has no sample yet. libRIST's first report lands one interval
  // after the stream starts, so an early read is empty rather than clean.
  bool valid = false;
  // The source carries the packet counters. False when only `quality` is
  // reported, which is the case the quality rule exists for.
  bool counters_valid = false;
  std::uint64_t sent = 0;
  std::uint64_t received = 0;
  std::uint64_t missing = 0;
  std::uint64_t recovered = 0;
  std::uint64_t lost = 0;
  std::uint32_t rtt_ms = 0;
  // 0-100; 100 means every packet arrived first attempt.
  double quality = 0.0;
};

// The leg's statistics, as the encoder gets them. `read` returns false when the
// source has nothing to report for the interval just run.
class stats_source
{
public:
  virtual ~stats_source() = default;
  virtual auto read(peer_sample& out) -> bool = 0;
};

// The payload driver: a short slice of the encode payload put on the leg at a
// fixed rate. The direct-mode implementation drives the encoder's own RIST
// sender; a test supplies the counters directly.
class payload_ramp
{
public:
  virtual ~payload_ramp() = default;
  // Start writing at kbps. False when the leg could not be opened at that rate.
  virtual auto start(int kbps) -> bool = 0;
  // Stop writing. Called on every path out of a run, including an abort.
  virtual void stop() = 0;
};

// ---- the ramp -------------------------------------------------------------

// Percent of the encode bitrate for each step, in ascending order.
inline constexpr int k_step_percent[] = {50, 75, 100, 125};

// Dwell per step. Four steps at this figure total the 5 s cap below.
inline constexpr int k_step_ms = 1250;

// Total time the ramp may occupy. A config asking for longer per-step dwell is
// truncated rather than allowed to overrun.
inline constexpr int k_max_total_ms = 5000;

// A step is clean on the quality rule at or above this figure.
inline constexpr double k_quality_floor = 99.0;

struct config
{
  // The encoder's configured encode rate. The ramp is a multiple of it, so this
  // must be set; zero plans nothing.
  int encode_kbps = 0;
  int step_ms = k_step_ms;
  int max_total_ms = k_max_total_ms;
};

struct step_plan
{
  int percent = 0;
  int kbps = 0;
};

auto plan(const config& cfg) -> std::vector<step_plan>;

// ---- the rule -------------------------------------------------------------

enum class verdict
{
  clean,
  lossy,
  // No usable sample for the step: nothing was proven either way.
  unknown,
};

// A step is clean when its loss counters are zero AND every packet that went
// missing was recovered -- the second half is what distinguishes a leg with
// retransmission headroom from one whose ceiling sat below the payload, where
// libRIST stops retransmitting and `lost` grows with `recovered` stuck at zero.
// A step that carried no packets proves nothing and is unknown.
//
// Without counters the step is judged on quality alone.
auto judge(const peer_sample& sample) -> verdict;

struct step_result
{
  int percent = 0;
  int kbps = 0;
  peer_sample stats {};
  verdict seen = verdict::unknown;
};

struct outcome
{
  // A figure was measured and is in upload_kbps.
  bool ok = false;
  // The ramp stopped before the plan finished. This carries no figure: a
  // partial ramp cannot say what the leg takes.
  bool aborted = false;
  int upload_kbps = 0;
  int percent = 0;
  // The figure came from the quality rule, not the loss counters.
  bool by_quality = false;
  std::vector<step_result> steps;
};

// The selection rule over one ramp, with no I/O. The measured upload is the
// highest step in an unbroken clean prefix: a step that fails ends the ramp,
// because capacity is the point at which delivery stops being clean, and a
// later step reading clean after a lossy one is noise, not headroom.
//
// A ramp that stopped short of the plan while still reading clean was aborted
// and yields no figure; so does `aborted`, whatever the run holds. A short run
// that ends on a failing step is a ramp that stopped when it should have.
auto select(const std::vector<step_plan>& planned,
            const std::vector<step_result>& run,
            bool aborted = false) -> outcome;

// ---- the runner -----------------------------------------------------------

struct hooks
{
  // Polled between steps and during a step's dwell. False aborts the ramp;
  // direct mode uses it to give way to a stream that starts mid-probe. Empty
  // means never abort.
  std::function<bool()> keep_running;
  // Dwell for a step. Empty sleeps for real, polling keep_running in slices.
  std::function<void(int)> sleep_ms;
};

// Runs the ramp and selects a figure. BLOCKING for the length of the ramp, so
// it belongs on a worker thread; the UI never calls it directly.
auto run(const config& cfg,
         stats_source& stats,
         payload_ramp& ramp,
         const hooks& h = {}) -> outcome;

}  // namespace upload_probe
