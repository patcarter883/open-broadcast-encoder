// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The transcode target's derivation, pinned without a portal or a link. It is
// the one place a hosted start body's transcode bitrate is decided, so the
// numbers the deployment actually saw are the cases asserted here.

#include "control/transcode_rate.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
// The deployed transport ceiling: the RIST bandwidth the encoder declares and
// sizes its recovery ring from (lib.h's rist_bandwidth_ceiling_kbps).
constexpr int kCeiling = 20000;
// The encoding rig's source rate on the cellular deployment.
constexpr int kSource = 7000;
}  // namespace

TEST_CASE("a transcode target above the source is lowered to it",
          "[control][transcode]")
{
  // The body that reached the encoder asked for 29 860 kbit/s of transcode from
  // a 7 Mbit/s source over a cellular uplink. A re-encode cannot carry more
  // detail than its input, so the source rate is the ceiling.
  CHECK(transcode_rate::body_kbps(29860, kSource, kCeiling) == kSource);
  CHECK(transcode_rate::cap_kbps(kSource, kCeiling) == kSource);
}

TEST_CASE("a target that already fits stays the portal's decision",
          "[control][transcode]")
{
  // 5 250 is what the portal's own source x factor rule derives for an H.265
  // transcode of a 7 Mbit/s feed. It is inside the cap, so it is untouched.
  CHECK(transcode_rate::body_kbps(5250, kSource, kCeiling) == 5250);
  CHECK(transcode_rate::body_kbps(kSource, kSource, kCeiling) == kSource);
}

TEST_CASE("a source above the transport ceiling is capped at 90% of it",
          "[control][transcode]")
{
  // The ceiling is what the chain is configured to carry, and the transcode
  // shares it with RIST overhead, retransmissions and sibling outputs.
  CHECK(transcode_rate::cap_kbps(40000, kCeiling) == 18000);
  CHECK(transcode_rate::body_kbps(29860, 40000, kCeiling) == 18000);
  // An unknown source rate is not a reason to exceed the ceiling either; the
  // caller decides that separately by passing zero.
  CHECK(transcode_rate::cap_kbps(0, kCeiling) == 0);
}

TEST_CASE("the floor keeps a small picture publishable", "[control][transcode]")
{
  // A 300 kbit/s source would derive a rate below the destination API's floor.
  CHECK(transcode_rate::cap_kbps(300, kCeiling)
        == transcode_rate::k_floor_kbps);
  CHECK(transcode_rate::body_kbps(29860, 300, kCeiling)
        == transcode_rate::k_floor_kbps);
  // A floor-sized source is unchanged.
  CHECK(transcode_rate::cap_kbps(transcode_rate::k_floor_kbps, kCeiling)
        == transcode_rate::k_floor_kbps);
}

TEST_CASE("a link below the floor wins over the floor", "[control][transcode]")
{
  // 400 kbit/s of transport can carry 360; asking for the 500 floor would be
  // the overrun this derivation exists to stop.
  CHECK(transcode_rate::cap_kbps(300, 400) == 360);
  CHECK(transcode_rate::body_kbps(6000, 300, 400) == 360);
  // Exactly at the boundary: 500 of transport carries a 450 target.
  CHECK(transcode_rate::cap_kbps(450, 500) == 450);
}

TEST_CASE("an unknown input leaves the portal's number alone",
          "[control][transcode]")
{
  // No source rate, no ceiling, or a negative either way: there is nothing to
  // derive from, and a guess would replace the portal's decision with one the
  // encoder cannot justify.
  CHECK(transcode_rate::cap_kbps(0, kCeiling) == 0);
  CHECK(transcode_rate::cap_kbps(kSource, 0) == 0);
  CHECK(transcode_rate::cap_kbps(-1, kCeiling) == 0);
  CHECK(transcode_rate::cap_kbps(kSource, -1) == 0);
  CHECK(transcode_rate::body_kbps(29860, 0, kCeiling) == 29860);
  CHECK(transcode_rate::body_kbps(29860, kSource, 0) == 29860);
}

TEST_CASE("a target the portal did not state is filled from the caps",
          "[control][transcode]")
{
  // An opted-in transcode with no bitrate (or zero) is a target the portal
  // could not derive; the encoder's cap is the only figure that exists.
  CHECK(transcode_rate::body_kbps(0, kSource, kCeiling) == kSource);
  CHECK(transcode_rate::body_kbps(-5, kSource, kCeiling) == kSource);
  // ... and with nothing to derive from, zero stays zero rather than becoming
  // a rate nobody chose.
  CHECK(transcode_rate::body_kbps(0, 0, 0) == 0);
}
