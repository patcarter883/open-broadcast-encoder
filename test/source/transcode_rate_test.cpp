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
// The encoding rig's configured source rate on the cellular deployment.
constexpr int kSource = 7000;
}  // namespace

TEST_CASE("a target the recipe states is published as stated",
          "[control][transcode]")
{
  // 29 860 for a 7 Mbit/s source is the recipe's decision for that destination.
  // It is published unchanged; the rate a link can carry is fixed in the recipe.
  CHECK(transcode_rate::body_kbps(29860, kSource, 0, kCeiling) == 29860);
  // A derived target is still bounded by what the source carries.
  CHECK(transcode_rate::resolve(kSource, 0, kCeiling) == kSource);
  CHECK(transcode_rate::body_kbps(0, kSource, 0, kCeiling) == kSource);
}

TEST_CASE("a target that already fits stays the portal's decision",
          "[control][transcode]")
{
  // 5 250 is what the portal's own source x factor rule derives for an H.265
  // transcode of a 7 Mbit/s feed. It is inside every bound, so it is untouched.
  CHECK(transcode_rate::body_kbps(5250, kSource, 0, kCeiling) == 5250);
  CHECK(transcode_rate::body_kbps(kSource, kSource, 0, kCeiling) == kSource);
}

TEST_CASE("a source above the transport ceiling is capped at 90% of it",
          "[control][transcode]")
{
  // The ceiling is what the chain is configured to carry, and the transcode
  // shares it with RIST overhead and retransmissions.
  CHECK(transcode_rate::resolve(40000, 0, kCeiling) == 18000);
  // Stated by the recipe: published as stated, above the sending rate included.
  CHECK(transcode_rate::body_kbps(29860, 40000, 0, kCeiling) == 29860);
}

TEST_CASE("a measured link outranks the plan and the sending rate",
          "[control][transcode]")
{
  // 4 200 measured on the WAN against an 8 000 source: the link wins, because a
  // rate it cannot carry is the failure this derivation exists to stop.
  CHECK(transcode_rate::resolve(8000, 4200, kCeiling) == 4200);
  CHECK(transcode_rate::body_kbps(29860, 8000, 4200, kCeiling) == 29860);
  CHECK(transcode_rate::body_kbps(0, 8000, 4200, kCeiling) == 4200);
  // A measurement above what is being sent cannot lift the target past the
  // source, and one above the ceiling's usable share cannot reach it either.
  CHECK(transcode_rate::resolve(8000, 12000, kCeiling) == 8000);
  CHECK(transcode_rate::resolve(8000, 12000, 6000) == 5400);
}

TEST_CASE("no measurement is deterministic from the source and ceiling",
          "[control][transcode]")
{
  // Zero budget means no link has been measured since launch; the answer is
  // still a bounded rate rather than the portal's plan-sized figure.
  CHECK(transcode_rate::resolve(8000, 0, kCeiling) == 8000);
  CHECK(transcode_rate::body_kbps(0, 8000, 0, kCeiling) == 8000);
  CHECK(transcode_rate::resolve(8000, 0, 6000) == 5400);
  // An unknown source rate does not license exceeding the ceiling either.
  CHECK(transcode_rate::resolve(0, 0, kCeiling) == 18000);
  CHECK(transcode_rate::body_kbps(0, 0, 0, kCeiling) == 18000);
}

TEST_CASE("the floor keeps a small picture publishable", "[control][transcode]")
{
  // A trickle of a measurement would derive a rate below the destination API's
  // floor, so the floor stands while it is still under every known bound.
  CHECK(transcode_rate::resolve(8000, 180, kCeiling)
        == transcode_rate::k_floor_kbps);
  CHECK(transcode_rate::body_kbps(0, 8000, 180, kCeiling)
        == transcode_rate::k_floor_kbps);
  // A floor-sized source is unchanged.
  CHECK(transcode_rate::resolve(transcode_rate::k_floor_kbps, 0, kCeiling)
        == transcode_rate::k_floor_kbps);
}

TEST_CASE("a link below the floor wins over the floor", "[control][transcode]")
{
  // 180 kbit/s of measured WAN sits under the floor, and asking for the floor
  // would be the overrun this derivation exists to stop. 400 of transport
  // carries 360.
  CHECK(transcode_rate::resolve(300, 180, 0) == 180);
  CHECK(transcode_rate::resolve(6000, 0, 400) == 360);
  // Exactly at the boundary: 500 of transport carries a 450 target.
  CHECK(transcode_rate::resolve(450, 0, 500) == 450);
}

TEST_CASE("nothing known leaves the portal's number alone",
          "[control][transcode]")
{
  // No source, no measurement and no ceiling: there is nothing to derive from,
  // and a guess would replace the portal's decision with one the encoder cannot
  // justify.
  CHECK(transcode_rate::resolve(0, 0, 0) == 0);
  CHECK(transcode_rate::body_kbps(29860, 0, 0, 0) == 29860);
}

TEST_CASE("a target the portal did not state is filled from the resolved rate",
          "[control][transcode]")
{
  // An opted-in transcode with no bitrate (or zero) is a target the portal
  // could not derive; the resolved rate is the only figure that exists.
  CHECK(transcode_rate::body_kbps(0, kSource, 0, kCeiling) == kSource);
  CHECK(transcode_rate::body_kbps(-5, kSource, 0, kCeiling) == kSource);
  // ... and with nothing to derive from, zero stays zero rather than becoming
  // a rate nobody chose.
  CHECK(transcode_rate::body_kbps(0, 0, 0, 0) == 0);
}
