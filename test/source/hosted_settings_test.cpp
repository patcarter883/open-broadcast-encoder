// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The hosted (portal) settings must survive a save/load round-trip. The
// backplane URL is the one field an operator types by hand, and an encoder that
// forgets it silently drops out of hosted mode on the next start.

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
        / ("obc-hosted-settings-test-" + std::to_string(::getpid()));
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

TEST_CASE("the backplane URL and hosted identity survive a round-trip",
          "[settings][hosted]")
{
  const temp_config_home home;

  library saved;
  saved.hosted.backplane_url = "https://api.stream.example.au";
  saved.hosted.device_token = "device-token-123";
  saved.hosted.device_id = 6;
  saved.hosted.bridge_id = 1;

  REQUIRE(settings::save(saved));

  library loaded;
  REQUIRE(settings::load(loaded));

  CHECK(loaded.hosted.backplane_url == "https://api.stream.example.au");
  CHECK(loaded.hosted.device_token == "device-token-123");
  CHECK(loaded.hosted.device_id == 6);
  CHECK(loaded.hosted.bridge_id == 1);
}

TEST_CASE("the encoder's instance name survives a round-trip",
          "[settings][identity]")
{
  const temp_config_home home;

  library saved;
  saved.encoder_name = "enc-studio-a";

  REQUIRE(settings::save(saved));

  library loaded;
  REQUIRE(settings::load(loaded));

  CHECK(loaded.encoder_name == "enc-studio-a");
}

TEST_CASE("the encoder defaults to a non-empty instance name",
          "[settings][identity]")
{
  library lib;
  CHECK_FALSE(lib.encoder_name.empty());
}

TEST_CASE("the bridge control settings survive a round-trip",
          "[settings][bridge]")
{
  const temp_config_home home;

  library saved;
  saved.bridge_ctl.address = "192.168.1.1";
  saved.bridge_ctl.bridge_uid = "rist2rist-60:be:b4:24:f2:76";
  saved.bridge_ctl.token = "pair-token-abc";
  saved.bridge_ctl.listen_url = "rist://192.168.1.1:5000";
  saved.bridge_ctl.listen_interface = "eth1";
  saved.bridge_ctl.forward_to = "rist://relay.example.au:20000";
  saved.bridge_ctl.interface_name = "wwan0";
  saved.bridge_ctl.encrypt_wan_leg = true;

  REQUIRE(settings::save(saved));

  library loaded;
  REQUIRE(settings::load(loaded));

  CHECK(loaded.bridge_ctl.address == "192.168.1.1");
  CHECK(loaded.bridge_ctl.bridge_uid == "rist2rist-60:be:b4:24:f2:76");
  CHECK(loaded.bridge_ctl.token == "pair-token-abc");
  CHECK(loaded.bridge_ctl.listen_url == "rist://192.168.1.1:5000");
  // The join interface is a device name on the ROUTER: a reload that dropped it
  // would silently stop the bridge joining the group and the media path would
  // go dark.
  CHECK(loaded.bridge_ctl.listen_interface == "eth1");
  CHECK(loaded.bridge_ctl.forward_to == "rist://relay.example.au:20000");
  CHECK(loaded.bridge_ctl.interface_name == "wwan0");
  CHECK(loaded.bridge_ctl.encrypt_wan_leg);
}

TEST_CASE("an unset join interface falls back to the documented default",
          "[settings][bridge]")
{
  const temp_config_home home;

  library saved;
  saved.bridge_ctl.listen_url = "rist://@239.1.1.10:5100";

  REQUIRE(settings::save(saved));

  library loaded;
  REQUIRE(settings::load(loaded));

  // eth1 is the router's LAN interface, which owns the multicast group; a
  // settings file that never named one still yields a joinable URL.
  CHECK(loaded.bridge_ctl.listen_interface == "eth1");
}

TEST_CASE("a persisted allocation can be read back and cleared",
          "[settings][hosted][session]")
{
  const temp_config_home home;

  hosted_session allocation;
  allocation.session_id = "s_test123";
  allocation.control_url = "https://relay.example.au/s/s_test123";
  allocation.control_token = "control-token-xyz";

  REQUIRE(settings::save_hosted_session(allocation));

  // The release path depends on reading this back: with no reader, a Stop or a
  // crash could never give the allocation back, and the node kept the receiver
  // and its RIST port for a session nobody was feeding.
  const auto loaded = settings::load_hosted_session();
  REQUIRE(loaded.has_value());
  CHECK(loaded->session_id == "s_test123");

  settings::clear_hosted_session();
  CHECK_FALSE(settings::load_hosted_session().has_value());
}
