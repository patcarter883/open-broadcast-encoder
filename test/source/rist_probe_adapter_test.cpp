// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The probe's adapter seam, pinned without libRIST: the sender-peer counters
// libRIST reports must reach the probe's rule as the rule expects them, and the
// sender the ramp builds must declare a ceiling sized for the ramp rather than
// for the payload it is not sending yet.

#include "calibrate/rist_probe.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a sender peer sample is judged on quality, not on counters",
          "[calibrate][probe]")
{
  const upload_probe::peer_sample sample =
      calibrate::peer_sample_from(calibrate::sender_counters {
          .sent = 1200,
          .received = 1200,
          .retransmitted = 0,
          .rtt_ms = 24,
          .quality = 100.0,
      });
  CHECK(sample.valid);
  // A sender peer carries no missing/recovered/lost: the rule must not be told
  // it has counters it does not have.
  CHECK_FALSE(sample.counters_valid);
  CHECK(sample.sent == 1200);
  CHECK(sample.received == 1200);
  CHECK(sample.rtt_ms == 24);
  CHECK(sample.quality == 100.0);
  CHECK(upload_probe::judge(sample) == upload_probe::verdict::clean);
}

TEST_CASE("a step libRIST had to skip traffic on reads lossy",
          "[calibrate][probe]")
{
  // quality is (sent / sent + skipped + retransmitted): below 99 the leg is
  // either retransmitting or dropping, and neither is headroom.
  const upload_probe::peer_sample sample =
      calibrate::peer_sample_from(calibrate::sender_counters {
          .sent = 1200,
          .received = 1200,
          .retransmitted = 90,
          .rtt_ms = 24,
          .quality = 93.0,
      });
  CHECK(upload_probe::judge(sample) == upload_probe::verdict::lossy);
}

TEST_CASE("a peer that acknowledged nothing cannot read clean",
          "[calibrate][probe]")
{
  // Sending into a hole: libRIST's own quality is computed from what it sent
  // and skipped, so it stays at 100 while no packet is acknowledged. A ramp
  // that believed that would report an uplink to a destination that is not
  // there.
  const upload_probe::peer_sample sample =
      calibrate::peer_sample_from(calibrate::sender_counters {
          .sent = 900,
          .received = 0,
          .retransmitted = 0,
          .rtt_ms = 0,
          .quality = 100.0,
      });
  CHECK(upload_probe::judge(sample) != upload_probe::verdict::clean);
}

TEST_CASE("no sample at all is unknown, not clean", "[calibrate][probe]")
{
  // libRIST reports one interval after the first packet, so an early read must
  // not read as a clean step.
  upload_probe::peer_sample sample;
  CHECK_FALSE(sample.valid);
  CHECK(upload_probe::judge(sample) == upload_probe::verdict::unknown);
}

TEST_CASE("the probe sender declares the top of the ramp plus headroom",
          "[calibrate][probe]")
{
  // 4000 kbps ramps to 5000 (125%); the ramp then tests the link, not the
  // sender's own ceiling, only if the declared ceiling clears the top step.
  const int top_step = 4000 * 125 / 100;
  CHECK(calibrate::probe_bandwidth_kbps(4000) == top_step + 5000);
  CHECK(calibrate::probe_bandwidth_kbps(4000) > top_step);
  // Nothing to encode is still a number the sender can be built with.
  CHECK(calibrate::probe_bandwidth_kbps(0) >= 100);
}

TEST_CASE("the pane states the figure or why there is none",
          "[calibrate][probe]")
{
  upload_probe::outcome ok;
  ok.ok = true;
  ok.upload_kbps = 5100;
  ok.percent = 125;
  CHECK(calibrate::format_outcome(ok) == "5100 kbps (125% of the encode rate)");

  upload_probe::outcome aborted;
  aborted.aborted = true;
  CHECK(calibrate::format_outcome(aborted)
        == "not measured - the ramp stopped early");

  const upload_probe::outcome none;
  CHECK(calibrate::format_outcome(none)
        == "no figure - the leg did not carry the first step");
}
