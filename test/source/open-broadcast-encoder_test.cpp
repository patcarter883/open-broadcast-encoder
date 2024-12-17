#include <catch2/catch_test_macros.hpp>

#include "lib.hpp"

TEST_CASE("Name is open-broadcast-encoder", "[library]")
{
  auto const lib = library {};
  REQUIRE(lib.name == "open-broadcast-encoder");
}
