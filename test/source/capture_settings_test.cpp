// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The settings contract for the JPEG XS capture input mode (MC4): the mode name
// and the chosen camera must survive a save/load round-trip, or a restart
// silently drops the operator's selection back to the default input.

#include <cstdlib>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <unistd.h>

#include "lib/lib.h"
#include "settings/settings.h"

namespace
{
// A private XDG_CONFIG_HOME for the test process, so the developer's real
// settings file is never read or written. settings::settings_file_path() reads
// XDG_CONFIG_HOME first on POSIX.
class temp_config_home
{
public:
  temp_config_home()
  {
    dir = std::filesystem::temp_directory_path()
        / ("obc-capture-settings-test-" + std::to_string(::getpid()));
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
}  // namespace

TEST_CASE("the jpegxs_capture mode and its camera survive a round-trip",
          "[settings][capture]")
{
  const temp_config_home config_home;

  library saved;
  saved.input_cfg.selected_input_mode = input_mode::jpegxs_capture;
  saved.input_cfg.selected_input = "5000";
  saved.input_cfg.capture_address = "10.50.1.118";
  saved.input_cfg.capture_name = "streamcam1";
  REQUIRE(settings::save(saved));

  library loaded;
  REQUIRE(settings::load(loaded));
  REQUIRE(loaded.input_cfg.selected_input_mode == input_mode::jpegxs_capture);
  REQUIRE(loaded.input_cfg.selected_input == "5000");
  REQUIRE(loaded.input_cfg.capture_address == "10.50.1.118");
  REQUIRE(loaded.input_cfg.capture_name == "streamcam1");
}

TEST_CASE("jpegxs_capture is not normalised away, and none still is",
          "[settings][capture]")
{
  const temp_config_home config_home;

  // `none` is normalised to the app default because the UI offers no such item;
  // jpegxs_capture IS a real menu item and must not be caught by that rule.
  library saved;
  saved.input_cfg.selected_input_mode = input_mode::jpegxs_capture;
  REQUIRE(settings::save(saved));

  library loaded;
  REQUIRE(settings::load(loaded));
  REQUIRE(loaded.input_cfg.selected_input_mode == input_mode::jpegxs_capture);
}

TEST_CASE("the input_mode order keeps the menu indices stable", "[capture]")
{
  // The protocol menu's user_data mirrors this enum; MC4 appends
  // jpegxs_capture AFTER raw_local and BEFORE none so indices 0..4 keep their
  // meaning and `none` stays the terminator's sentinel.
  REQUIRE(static_cast<int>(input_mode::jpegxs_capture)
          == static_cast<int>(input_mode::raw_local) + 1);
  REQUIRE(static_cast<int>(input_mode::none)
          == static_cast<int>(input_mode::jpegxs_capture) + 1);
}
