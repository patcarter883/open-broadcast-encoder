// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The bonding contract in the settings file: the uplink list must survive a
// save/load round-trip, and its shape must be the documented one, because an
// operator can also write that file by hand.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <unistd.h>

#include "lib/lib.h"
#include "settings/settings.h"

namespace
{
class temp_config_home
{
public:
  temp_config_home()
  {
    dir = std::filesystem::temp_directory_path()
        / ("obc-uplink-settings-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    ::setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
  }
  ~temp_config_home()
  {
    ::unsetenv("XDG_CONFIG_HOME");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  temp_config_home(const temp_config_home&) = delete;
  temp_config_home& operator=(const temp_config_home&) = delete;
  temp_config_home(temp_config_home&&) = delete;
  temp_config_home& operator=(temp_config_home&&) = delete;

private:
  std::filesystem::path dir;
};

auto read_settings_file() -> std::string
{
  std::ifstream file(settings::settings_file_path());
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

// The file is written indented, so the key/value spelling is matched once the
// layout whitespace is gone rather than against one dump() style.
auto without_whitespace(std::string text) -> std::string
{
  text.erase(std::remove_if(text.begin(),
                            text.end(),
                            [](unsigned char c) { return std::isspace(c); }),
             text.end());
  return text;
}
}  // namespace

TEST_CASE("the uplink list survives a round-trip", "[settings][bond]")
{
  const temp_config_home config_home;

  library saved;
  saved.output_cfg.uplinks = {
      uplink::config {"wwan0", true, 100},
      uplink::config {"wwan1", false, 25},
  };
  REQUIRE(settings::save(saved));

  // The shape, on screen: the output block as it lands on disk.
  std::cout << "settings.json (output block):\n"
            << read_settings_file() << "\n";

  library loaded;
  REQUIRE(settings::load(loaded));
  REQUIRE(loaded.output_cfg.uplinks.size() == 2);
  CHECK(loaded.output_cfg.uplinks[0].name == "wwan0");
  CHECK(loaded.output_cfg.uplinks[0].weight_is_auto);
  CHECK(loaded.output_cfg.uplinks[1].name == "wwan1");
  CHECK_FALSE(loaded.output_cfg.uplinks[1].weight_is_auto);
  CHECK(loaded.output_cfg.uplinks[1].fixed_weight == 25);
}

TEST_CASE("no uplinks is written as an empty array, and loads back as none",
          "[settings][bond]")
{
  const temp_config_home config_home;

  library saved;
  REQUIRE(saved.output_cfg.uplinks.empty());
  REQUIRE(settings::save(saved));

  // An empty array, not a missing key: the file states that this encoder sends
  // the single default-route leg, and a reader does not have to know a default.
  const std::string text = without_whitespace(read_settings_file());
  CHECK(text.find("\"uplinks\":[]") != std::string::npos);

  library loaded;
  loaded.output_cfg.uplinks.push_back(uplink::config {"stale", true, 10});
  REQUIRE(settings::load(loaded));
  CHECK(loaded.output_cfg.uplinks.empty());
}

TEST_CASE("an uplink entry that names no adapter is dropped on load",
          "[settings][bond]")
{
  const temp_config_home config_home;

  library saved;
  // A nameless uplink resolves to no miface and would refuse every Start, so it
  // never survives a load.
  saved.output_cfg.uplinks = {
      uplink::config {"", true, 100},
      uplink::config {"wwan0", true, 100},
  };
  REQUIRE(settings::save(saved));

  library loaded;
  REQUIRE(settings::load(loaded));
  REQUIRE(loaded.output_cfg.uplinks.size() == 1);
  CHECK(loaded.output_cfg.uplinks[0].name == "wwan0");
}
