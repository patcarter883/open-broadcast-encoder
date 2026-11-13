// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The measured uplink in the settings file. It is not a user setting, so the
// assertions are about survival, not about editing: a figure and its record of
// when and where it was taken must come back, and a file written before the
// probe existed must still load with no measurement rather than a stale one.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
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
        / ("obc-upload-settings-test-" + std::to_string(::getpid()));
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

auto read_settings() -> nlohmann::json
{
  std::ifstream file(settings::settings_file_path());
  REQUIRE(file);
  nlohmann::json root;
  file >> root;
  return root;
}
}  // namespace

TEST_CASE("the measured uplink survives a round-trip", "[settings][upload]")
{
  const temp_config_home config_home;

  library saved;
  saved.output_cfg.address = "rist://198.51.100.9:5100";
  saved.upload =
      upload_measurement {7400, "rist://198.51.100.9:5100", "2026-11-13 14:32"};
  REQUIRE(settings::save(saved));

  // The keys, as they land on disk: under the output block, beside the address
  // whose path they describe.
  const nlohmann::json root = read_settings();
  REQUIRE(root.contains("output"));
  CHECK(root["output"]["direct_upload_kbps"] == 7400);
  CHECK(root["output"]["direct_upload_destination"]
        == "rist://198.51.100.9:5100");
  CHECK(root["output"]["direct_upload_measured_at"] == "2026-11-13 14:32");

  library loaded;
  REQUIRE(settings::load(loaded));
  CHECK(loaded.upload.valid());
  CHECK(loaded.upload.kbps == 7400);
  CHECK(loaded.upload.destination == "rist://198.51.100.9:5100");
  CHECK(loaded.upload.taken_at == "2026-11-13 14:32");
}

TEST_CASE("a settings file from before the probe loads with no measurement",
          "[settings][upload]")
{
  const temp_config_home config_home;

  // Exactly what an older encoder wrote: no measurement keys at all.
  nlohmann::json root;
  root["output"] = {
      {"address", "rist://203.0.113.4:6000"},
      {"host", "203.0.113.4"},
      {"port", 6000},
      {"bandwidth", 12000},
  };
  std::error_code ec;
  std::filesystem::create_directories(
      settings::settings_file_path().parent_path(), ec);
  std::ofstream(settings::settings_file_path()) << root.dump(2) << '\n';

  library loaded;
  REQUIRE(settings::load(loaded));
  // Not "measured 0 kbps": the fields keep their defaults, which is unmeasured.
  CHECK_FALSE(loaded.upload.valid());
  CHECK(loaded.upload.kbps == 0);
  CHECK(loaded.upload.destination.empty());
  CHECK(loaded.upload.taken_at.empty());
  // The rest of the output block is untouched by the absent keys.
  CHECK(loaded.output_cfg.address == "rist://203.0.113.4:6000");
  CHECK(loaded.output_cfg.bandwidth == 12000);
}

TEST_CASE("a negative or zero figure is not carried as a measurement",
          "[settings][upload]")
{
  const temp_config_home config_home;

  nlohmann::json root;
  root["output"] = {
      {"direct_upload_kbps", -5},
      {"direct_upload_destination", "rist://203.0.113.4:6000"},
      {"direct_upload_measured_at", "2026-11-13 14:32"},
  };
  std::error_code ec;
  std::filesystem::create_directories(
      settings::settings_file_path().parent_path(), ec);
  std::ofstream(settings::settings_file_path()) << root.dump(2) << '\n';

  library loaded;
  REQUIRE(settings::load(loaded));
  CHECK_FALSE(loaded.upload.valid());
  // The record goes with the figure: a timestamp for a measurement that no
  // longer exists would read as evidence it was taken.
  CHECK(loaded.upload.taken_at.empty());
  CHECK(loaded.upload.destination.empty());
}
