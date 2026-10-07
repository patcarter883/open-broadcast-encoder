// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "stats/bitrate_scale.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "lib/lib.h"

namespace bitrate_scale
{
namespace
{

auto steady_ms() -> int64_t
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

auto scale_locked(double quality,
                  cumulative_stats* stats,
                  const encode_config& encode_config,
                  int* new_bitrate_out) -> bool
{
  if (stats == nullptr) {
    return false;
  }
  if (encode_config.bitrate.load(std::memory_order_relaxed) <= 0) {
    return false;
  }
  if (std::isnan(quality) || std::isinf(quality)) {
    return false;
  }

  // --- the decision window -------------------------------------------------
  // Accumulate the quality seen, and only decide on the tick. Two reasons: a
  // burst of samples must not write the bitrate property hundreds of times a
  // second, and the MEAN over the window is what the controller acts on.
  //
  // The mean, not the worst sample: the figure flaps across its whole range on
  // a bonded link (reordering counts as loss), so a window worst is essentially
  // always below the previous window's — which makes every decision a decrease
  // and leaves the climb branch unreachable. That is a ratchet to the floor.
  // A mean absorbs a transient dip while still showing a sustained drop within
  // one tick, which is the response that is actually wanted.
  stats->window_quality_sum += quality;
  stats->window_quality_count += 1;
  const int64_t now = steady_ms();
  if (stats->last_decision_ms != 0
      && now - stats->last_decision_ms < k_decision_interval_ms) {
    return false;  // still inside the window: keep accumulating
  }
  const double sampled = stats->window_quality_count > 0
                             ? stats->window_quality_sum
                                   / stats->window_quality_count
                             : quality;
  stats->window_quality_sum = 0.0;  // start the next window empty
  stats->window_quality_count = 0;
  stats->last_decision_ms = now;

  int bitrateDelta = 0;
  double qualDiffPct = 0.0;
  int adjBitrate = 0;
  const double maxBitrate = static_cast<double>(
      encode_config.bitrate.load(std::memory_order_relaxed));
  bool returnVal = false;

  // Quality moved: scale the working bitrate by the same ratio. The int cast on
  // the comparison means sub-1-point wobble is ignored, so a jittery link does
  // not thrash the encoder. Acting on the window worst also means a section
  // that merely flaps cannot repeatedly re-trigger this, because once the
  // window worst settles the comparison settles with it.
  if (stats->previous_quality > 0.0
      && static_cast<int>(sampled) != static_cast<int>(stats->previous_quality))
  {
    qualDiffPct = sampled / stats->previous_quality;
    adjBitrate = static_cast<int>(stats->current_bitrate * qualDiffPct);
    bitrateDelta = adjBitrate - stats->current_bitrate;
  }

  // Perfect link and still below the ceiling: climb proportionally to how far
  // below we are (slow at the bottom, faster near the top).
  if (static_cast<int>(stats->previous_quality) == 100
      && static_cast<int>(sampled) == 100
      && static_cast<double>(stats->current_bitrate) < maxBitrate
      && maxBitrate > 0.0)
  {
    qualDiffPct = static_cast<double>(stats->current_bitrate) / maxBitrate;
    adjBitrate = static_cast<int>(stats->current_bitrate * (1.0 + qualDiffPct));
    bitrateDelta = adjBitrate - stats->current_bitrate;
  }

  // Fast down, slow up: a decrease is applied on the decision that sees it, an
  // increase waits out the hold-off since the last decrease.
  if (bitrateDelta > 0
      && now - stats->last_decrease_ms < k_increase_holdoff_ms) {
    bitrateDelta = 0;
  }

  if (bitrateDelta != 0
      || maxBitrate < static_cast<double>(stats->current_bitrate))
  {
    // Damp by 2 so one bad window cannot halve the bitrate, and clamp to
    // [k_min_bitrate_kbps, ceiling].
    int candidate = stats->current_bitrate + bitrateDelta / 2;
    int newBitrate = std::max(
        std::min(candidate, static_cast<int>(maxBitrate)), k_min_bitrate_kbps);
    if (newBitrate < stats->current_bitrate) {
      stats->last_decrease_ms = now;  // re-arm the hold-off on every drop
    }
    stats->current_bitrate = newBitrate;
    returnVal = true;
  }

  stats->previous_quality = sampled;

  if (new_bitrate_out != nullptr) {
    *new_bitrate_out = returnVal ? stats->current_bitrate : 0;
  }

  return returnVal;
}

}  // namespace bitrate_scale
