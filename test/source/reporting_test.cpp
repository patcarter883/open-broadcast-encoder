// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The backplane's bridge-report contract: the body's shape, and what the
// encoder does with each answer. Scripted transport -- no server.

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "bridge/reporting.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace
{
using nlohmann::json;

auto advertised(std::map<std::string, std::string> txt = {},
                const std::string& address = "192.168.8.1")
    -> bridge::mdns::service
{
  bridge::mdns::service service;
  service.instance = "rist2rist-11:22:33:44:55:66";
  service.host = "OpenWrt.lan";
  service.address = address;
  service.port = 5000;
  service.txt = std::move(txt);
  return service;
}

// Records what was sent and replays one canned answer.
struct fake_backplane
{
  std::string method;
  std::string path;
  std::string token;
  json body;
  int calls = 0;

  int status = 201;
  std::string response =
      json {{"ok", true}, {"bridge", json {{"id", 7}}}}.dump();

  auto transport() -> bridge::backplane_transport_fn
  {
    return [this](const std::string& method,
                  const std::string& path,
                  const std::string& token,
                  const std::string& body) -> std::pair<int, std::string>
    {
      this->method = method;
      this->path = path;
      this->token = token;
      ++calls;
      try {
        this->body = json::parse(body);
      } catch (const json::exception&) {
        this->body = json::object();
      }
      return {status, response};
    };
  }
};
}  // namespace

// ------------------------------------------------------------------ the body

TEST_CASE("the report names the bridge and its address", "[reporting]")
{
  bridge::bridge_report report;
  report.address = "192.168.8.1";
  report.managed = true;

  const auto body =
      bridge::bridge_report_body(advertised({{"api", "1"}}), report, "");

  REQUIRE(body["bridge_uid"] == "rist2rist-11:22:33:44:55:66");
  REQUIRE(body["lan_host"] == "192.168.8.1");
  REQUIRE(body["api_version"] == 1);
}

TEST_CASE("managed is nested in the reported config, never top level",
          "[reporting]")
{
  // The backplane mirrors `managed` from reported_config.managed precisely so
  // that a caller cannot assert it. Sending it top-level would be silently
  // ignored -- or worse, imply a control we do not have.
  bridge::bridge_report report;
  report.address = "192.168.8.1";
  report.managed = true;

  const auto body = bridge::bridge_report_body(advertised(), report, "");

  REQUIRE_FALSE(body.contains("managed"));
  REQUIRE(body["reported_config"]["managed"] == true);
}

TEST_CASE("the bridge's own config is reported, not overwritten", "[reporting]")
{
  bridge::bridge_report report;
  report.address = "192.168.8.1";
  report.managed = false;
  report.reported_config =
      json {{"listen_url", "rist://0.0.0.0:6000"}, {"running", true}};

  const auto body = bridge::bridge_report_body(advertised(), report, "");

  REQUIRE(body["reported_config"]["listen_url"] == "rist://0.0.0.0:6000");
  REQUIRE(body["reported_config"]["running"] == true);
  REQUIRE(body["reported_config"]["managed"] == false);
}

TEST_CASE("a null reported config becomes an object, not a null", "[reporting]")
{
  // The backplane validates reported_config as an array/object; null would be
  // rejected and the report lost.
  bridge::bridge_report report;
  report.address = "192.168.8.1";

  const auto body = bridge::bridge_report_body(advertised(), report, "");

  REQUIRE(body["reported_config"].is_object());
}

TEST_CASE("health is ok unless there is an error to report", "[reporting]")
{
  bridge::bridge_report clean;
  clean.address = "192.168.8.1";
  REQUIRE(bridge::bridge_report_body(advertised(), clean, "")["health"]["ok"]
          == true);

  bridge::bridge_report broken;
  broken.address = "192.168.8.1";
  broken.last_error = "the bridge refused the call";
  const auto body = bridge::bridge_report_body(advertised(), broken, "");
  REQUIRE(body["health"]["ok"] == false);
  REQUIRE(body["health"]["last_error"] == "the bridge refused the call");
}

TEST_CASE("the token is sent ONLY when this run claimed the bridge",
          "[reporting]")
{
  bridge::bridge_report report;
  report.address = "192.168.8.1";

  // A routine health update must not touch the stored token.
  REQUIRE_FALSE(bridge::bridge_report_body(advertised(), report, "")
                    .contains("pair_token"));

  // The claim is the one moment the portal can learn it.
  const auto claimed =
      bridge::bridge_report_body(advertised(), report, "tok-NEW");
  REQUIRE(claimed["pair_token"] == "tok-NEW");
}

TEST_CASE("an address of unknown shape is simply omitted", "[reporting]")
{
  bridge::bridge_report report;  // no address
  const auto body = bridge::bridge_report_body(advertised(), report, "");

  REQUIRE_FALSE(body.contains("lan_host"));
}

TEST_CASE("api_version is an integer, defaulting sanely", "[reporting]")
{
  SECTION("from TXT")
  {
    REQUIRE(bridge::api_version_number(advertised({{"api", "2"}})) == 2);
  }
  SECTION("absent")
  {
    REQUIRE(bridge::api_version_number(advertised()) == 1);
  }
  SECTION("not a number")
  {
    REQUIRE(bridge::api_version_number(advertised({{"api", "newest"}})) == 1);
  }
  SECTION("out of the range the backplane accepts")
  {
    REQUIRE(bridge::api_version_number(advertised({{"api", "70000"}})) == 1);
    REQUIRE(bridge::api_version_number(advertised({{"api", "0"}})) == 1);
  }
}

// ---------------------------------------------------------------- the report

TEST_CASE("a report posts to the bridges collection with the device token",
          "[reporting]")
{
  fake_backplane backplane;
  bridge::bridge_reporter reporter(
      "https://api.example.au", "dev-token", backplane.transport());

  bridge::bridge_report report;
  report.address = "192.168.8.1";

  const auto result = reporter.report(advertised(), report, "");

  REQUIRE(result.ok);
  REQUIRE(result.bridge_id == 7);
  REQUIRE(backplane.method == "POST");
  REQUIRE(backplane.path == "/api/v1/bridges");
  REQUIRE(backplane.token == "dev-token");
}

TEST_CASE("a 200 and a 201 are both a success", "[reporting]")
{
  fake_backplane backplane;
  bridge::bridge_reporter reporter(
      "https://api.example.au", "dev-token", backplane.transport());
  bridge::bridge_report report;

  backplane.status = 201;
  REQUIRE(reporter.report(advertised(), report, "").ok);

  backplane.status = 200;
  REQUIRE(reporter.report(advertised(), report, "").ok);
}

TEST_CASE("a rejected report carries the backplane's reason", "[reporting]")
{
  fake_backplane backplane;
  backplane.status = 422;
  backplane.response = json {{"ok", false}, {"message", "bad lan_host"}}.dump();
  bridge::bridge_reporter reporter(
      "https://api.example.au", "dev-token", backplane.transport());

  const auto result =
      reporter.report(advertised(), bridge::bridge_report {}, "");

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "bad lan_host");
  REQUIRE(result.http_status == 422);
}

TEST_CASE("an unreachable backplane is a named failure", "[reporting]")
{
  fake_backplane backplane;
  backplane.status = 0;
  backplane.response.clear();
  bridge::bridge_reporter reporter(
      "https://api.example.au", "dev-token", backplane.transport());

  const auto result =
      reporter.report(advertised(), bridge::bridge_report {}, "");

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "no response from the backplane");
}

TEST_CASE("a non-JSON answer is refused rather than assumed", "[reporting]")
{
  fake_backplane backplane;
  backplane.status = 200;
  backplane.response = "<html>nope</html>";
  bridge::bridge_reporter reporter(
      "https://api.example.au", "dev-token", backplane.transport());

  const auto result =
      reporter.report(advertised(), bridge::bridge_report {}, "");

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "the backplane did not return JSON");
}

// ------------------------------------------------------------ the credential

TEST_CASE("the credential is fetched by the backplane's bridge id",
          "[reporting]")
{
  fake_backplane backplane;
  backplane.status = 200;
  backplane.response =
      json {{"ok", true},
            {"credential",
             json {{"bridge_uid", "rist2rist-aa"}, {"token", "tok-1"}}}}
          .dump();
  bridge::bridge_reporter reporter(
      "https://api.example.au", "dev-token", backplane.transport());

  std::string error;
  const auto token = reporter.credential(7, error);

  REQUIRE(token == "tok-1");
  REQUIRE(error.empty());
  REQUIRE(backplane.method == "POST");
  REQUIRE(backplane.path == "/api/v1/bridges/7/credential");
  REQUIRE(backplane.token == "dev-token");
}

TEST_CASE("no token held is an ordinary answer, not a failure", "[reporting]")
{
  // 409 means the bridge is virgin and must be claimed first. Treating it as an
  // error would send the operator chasing a non-problem.
  fake_backplane backplane;
  backplane.status = 409;
  backplane.response = json {{"ok", false},
                             {"error_code", "no_token"},
                             {"message", "no token is held for this bridge"}}
                           .dump();
  bridge::bridge_reporter reporter(
      "https://api.example.au", "dev-token", backplane.transport());

  std::string error;
  const auto token = reporter.credential(7, error);

  REQUIRE(token.empty());
  REQUIRE(error == "no_token");
}

TEST_CASE("a cross-account or missing bridge is reported as such",
          "[reporting]")
{
  fake_backplane backplane;
  backplane.status = 404;
  backplane.response = json {{"ok", false}, {"error_code", "not_found"}}.dump();
  bridge::bridge_reporter reporter(
      "https://api.example.au", "dev-token", backplane.transport());

  std::string error;
  const auto token = reporter.credential(99, error);

  REQUIRE(token.empty());
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("a bridge reporter with no transport fails cleanly", "[reporting]")
{
  bridge::bridge_reporter reporter("https://api.example.au", "dev-token");

  const auto result =
      reporter.report(advertised(), bridge::bridge_report {}, "");
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error == "no transport is configured");

  std::string error;
  REQUIRE(reporter.credential(1, error).empty());
  REQUIRE(error == "no transport is configured");
}
