// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "lib/lib.h"

// `library` is app_context's central object: one per process, holding the
// configs, stats and thread handles that run_loop(), stop() and the RIST
// callback threads share. There is no behaviour to exercise without a pipeline,
// so what this pins is the construct-and-destroy contract.
TEST_CASE("library starts stopped and ready to run", "[library]")
{
  library const lib {};

  REQUIRE(lib.is_running == false);
  REQUIRE(lib.preview_running == false);
  REQUIRE(lib.run_flag != nullptr);
  REQUIRE(*lib.run_flag == false);
  REQUIRE(lib.threads.empty());
}

// Held by pointer and shared across threads, so a copy would give two owners of
// one run_flag and one set of thread handles. lib.h deletes all four; this pins
// that so nobody re-adds them by accident.
TEST_CASE("library is neither copyable nor movable", "[library]")
{
  STATIC_REQUIRE(!std::is_copy_constructible_v<library>);
  STATIC_REQUIRE(!std::is_copy_assignable_v<library>);
  STATIC_REQUIRE(!std::is_move_constructible_v<library>);
  STATIC_REQUIRE(!std::is_move_assignable_v<library>);
}
