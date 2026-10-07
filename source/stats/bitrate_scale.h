// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The adaptive-bitrate step, in exactly ONE definition.
//
// Extracted from stats::scale_encoder_bitrate_locked so the bonding rig can
// exercise the SAME code the encoder runs. A second copy inside the test tool
// would pass while production drifted.
//
// Deliberately free of FLTK and GStreamer: it needs only the two structs
// below, and that is what lets a headless test binary link it.
#pragma once

#include <cstdint>

struct cumulative_stats;
struct encode_config;

namespace bitrate_scale
{

// --- control policy: the numbers to tune, defined once ---------------------
//
// FAST DOWN, SLOW UP. A failing link is relieved on the decision that sees the
// drop; a recovery has to earn the bitrate back, because climbing eagerly on a
// noisy quality figure is what turns a dip into an oscillation.
//
// The quality figure is a PER-INTERVAL ratio and arrives at the transport's
// rate — measured on the rig at ~107 samples/s, NOT the "once every second"
// that RISTNet.h:281 claims — and on a bonded link with unequal leg latency it
// flaps across its whole range. So the controller does not act per sample: it
// acts on a fixed tick, on the MEAN quality over that tick. That bounds how
// often the encoder's bitrate property can be written (the thrash a live
// encoder cannot survive), absorbs a transient dip, and still shows a sustained
// drop within one tick. Do NOT use the window's worst sample here: on a
// flapping link that is always below the previous window's, so every decision
// becomes a decrease and the controller ratchets to the floor.
constexpr int64_t k_decision_interval_ms = 500;

// After any decrease, no increase for this long. Without it the asymmetric
// fast-down/slow-up pair ratchets: every new low takes a step down while the
// climb is still held off, so a flapping-but-working link walks to the floor.
constexpr int64_t k_increase_holdoff_ms = 2000;

// Floor for the working bitrate. The ceiling is encode_config.bitrate.
constexpr int k_min_bitrate_kbps = 1000;

// One ABR step. `quality` is 0-100 (100 = every packet went first-attempt).
// The caller MUST hold stats->mutex: the decision-window state lives in stats.
//
// Returns true when the bitrate changed, and writes the new bitrate to
// *new_bitrate_out; on no change *new_bitrate_out is set to 0, so callers never
// re-read current_bitrate under a second lock.
auto scale_locked(double quality,
                  cumulative_stats* stats,
                  const encode_config& encode_config,
                  int* new_bitrate_out) -> bool;

}  // namespace bitrate_scale
