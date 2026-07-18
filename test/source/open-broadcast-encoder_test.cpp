// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <catch2/catch_test_macros.hpp>

#include "lib.h"

TEST_CASE("Name is open-broadcast-encoder", "[library]")
{
  auto const lib = library {};
  REQUIRE(lib.name == "open-broadcast-encoder");
}
