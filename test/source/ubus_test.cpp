// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The rist2rist ubus contract (DT-19, DT-21): envelope shape, reply parsing,
// and the token rules. Scripted transport -- no router, no LAN.

#include <string>
#include <utility>
#include <vector>

#include "bridge/ubus.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace
{
using nlohmann::json;

// Records every request and replays one canned reply.
auto scripted(std::vector<std::string>& seen, int status, std::string body)
    -> bridge::ubus_client::transport_fn
{
  return [&seen, status, body = std::move(body)](
             const std::string& url,
             const std::string& request) -> std::pair<int, std::string>
  {
    seen.push_back(url + "|" + request);
    return {status, body};
  };
}

// A well-formed ubus reply carrying the plugin's payload.
auto ubus_ok(const json& payload) -> std::string
{
  return json {
      {"jsonrpc", "2.0"}, {"id", 1}, {"result", json::array({0, payload})}}
      .dump();
}
}  // namespace

TEST_CASE("the envelope uses the zero session and names the service", "[ubus]")
{
  const json request = json::parse(
      bridge::build_request("claim", json {{"device_uid", "enc-1"}}));

  REQUIRE(request["jsonrpc"] == "2.0");
  REQUIRE(request["method"] == "call");
  REQUIRE(request["params"].is_array());
  REQUIRE(request["params"].size() == 4);
  // No session at all: the ACL exposes these methods to the unauthenticated
  // group and the plugin authenticates on the token instead.
  REQUIRE(request["params"][0] == bridge::k_no_session);
  REQUIRE(request["params"][1] == "rist2rist");
  REQUIRE(request["params"][2] == "claim");
  REQUIRE(request["params"][3]["device_uid"] == "enc-1");
}

TEST_CASE("build_request always emits an argument object", "[ubus]")
{
  const json request =
      json::parse(bridge::build_request("reload", json::array()));
  // A non-object argument would be rejected by ubus, so it is coerced.
  REQUIRE(request["params"][3].is_object());
  REQUIRE(request["params"][3].empty());
}

TEST_CASE("the token rides in the arguments, not a header", "[ubus]")
{
  std::vector<std::string> seen;
  bridge::ubus_client client("http://192.168.8.1",
                             "tok-abc123",
                             scripted(seen, 200, ubus_ok(json {{"ok", true}})));

  client.reconcile(json {{"listen_url", "rist://0.0.0.0:6000"}});

  REQUIRE(seen.size() == 1);
  const auto separator = seen[0].find('|');
  REQUIRE(seen[0].substr(0, separator) == "http://192.168.8.1/ubus");

  const json request = json::parse(seen[0].substr(separator + 1));
  REQUIRE(request["params"][3]["token"] == "tok-abc123");
  REQUIRE(request["params"][3]["listen_url"] == "rist://0.0.0.0:6000");
}

TEST_CASE("claim sends no token, because claiming is what establishes one",
          "[ubus]")
{
  std::vector<std::string> seen;
  bridge::ubus_client client("http://192.168.8.1",
                             {},
                             scripted(seen, 200, ubus_ok(json {{"ok", true}})));

  client.claim("enc-1");

  const json request = json::parse(seen[0].substr(seen[0].find('|') + 1));
  REQUIRE(request["params"][2] == "claim");
  REQUIRE_FALSE(request["params"][3].contains("token"));
}

TEST_CASE("each method maps to its rist2rist name", "[ubus]")
{
  std::vector<std::string> seen;
  bridge::ubus_client client("http://192.168.8.1",
                             "tok-abc123",
                             scripted(seen, 200, ubus_ok(json {{"ok", true}})));

  client.get_config();
  client.reload();
  client.release();

  REQUIRE(seen.size() == 3);
  REQUIRE(json::parse(seen[0].substr(seen[0].find('|') + 1))["params"][2]
          == "get_config");
  REQUIRE(json::parse(seen[1].substr(seen[1].find('|') + 1))["params"][2]
          == "reload");
  REQUIRE(json::parse(seen[2].substr(seen[2].find('|') + 1))["params"][2]
          == "release");
}

TEST_CASE("a successful call reports ok and exposes the payload", "[ubus]")
{
  auto result = bridge::parse_response(
      200, ubus_ok(json {{"ok", true}, {"claimed", false}, {"running", true}}));

  REQUIRE(result.ok);
  REQUIRE(result.ubus_status == 0);
  REQUIRE(result.data["claimed"] == false);
  REQUIRE(result.error.empty());
}

TEST_CASE("the plugin's own refusal is surfaced verbatim", "[ubus]")
{
  // This is the important case: ubus succeeded, the PLUGIN refused. The status
  // is 0 and everything that matters is in the payload.
  auto result = bridge::parse_response(
      200,
      ubus_ok(json {{"ok", false},
                    {"error", "bad_token"},
                    {"message", "the pair token is not valid"}}));

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.ubus_status == 0);
  REQUIRE(result.error_code == "bad_token");
  REQUIRE(result.error == "the pair token is not valid");
}

TEST_CASE("a plugin refusal without a message still names a code", "[ubus]")
{
  auto result = bridge::parse_response(200, ubus_ok(json {{"ok", false}}));

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error_code == "failed");
}

TEST_CASE("a ubus-level denial never reads as success", "[ubus]")
{
  // What the ACL returns when the methods are NOT exposed: result[0] = 6 and no
  // plugin payload at all.
  auto result = bridge::parse_response(
      200,
      ubus_ok(json::object()).substr(0, 0)
          + json {{"jsonrpc", "2.0"},
                  {"id", 1},
                  {"result", json::array({6, json::object()})}}
                .dump());

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.ubus_status == 6);
  REQUIRE(result.error_code == "ubus_6");
  REQUIRE(result.error == "permission denied");
}

TEST_CASE("an unnamed ubus status is reported numerically, not guessed",
          "[ubus]")
{
  auto result = bridge::parse_response(
      200,
      json {{"jsonrpc", "2.0"},
            {"id", 1},
            {"result", json::array({99, json::object()})}}
          .dump());

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error_code == "ubus_99");
  REQUIRE(result.error.find("99") != std::string::npos);
}

TEST_CASE("every transport failure names itself", "[ubus]")
{
  SECTION("no response")
  {
    auto result = bridge::parse_response(0, "");
    REQUIRE(result.error_code == "no_response");
    REQUIRE(result.http_status == 0);
  }
  SECTION("uhttpd answered for itself")
  {
    auto result = bridge::parse_response(403, "<html>Forbidden</html>");
    REQUIRE(result.error_code == "http_403");
    REQUIRE(result.error.find("403") != std::string::npos);
  }
  SECTION("not JSON at all")
  {
    auto result = bridge::parse_response(200, "not json");
    REQUIRE(result.error_code == "malformed_reply");
  }
  SECTION("JSON, but not JSON-RPC")
  {
    auto result = bridge::parse_response(200, json {{"hello", "world"}}.dump());
    REQUIRE(result.error_code == "unexpected_reply");
  }
  SECTION("JSON-RPC with a non-object payload")
  {
    auto result = bridge::parse_response(
        200,
        json {
            {"jsonrpc", "2.0"}, {"id", 1}, {"result", json::array({0, "nope"})}}
            .dump());
    REQUIRE(result.error_code == "unexpected_reply");
  }
}

TEST_CASE("claim_token takes the token only from a successful claim", "[ubus]")
{
  SECTION("success")
  {
    auto result = bridge::parse_response(
        200,
        ubus_ok(json {{"ok", true}, {"token", "tok-NEW"}, {"claimed", true}}));
    REQUIRE(bridge::claim_token(result) == "tok-NEW");
  }
  SECTION("a refused claim yields nothing")
  {
    // The whole point: a bridge that is already claimed must not hand over a
    // token.
    auto result = bridge::parse_response(
        200, ubus_ok(json {{"ok", false}, {"error", "already_claimed"}}));
    REQUIRE(bridge::claim_token(result).empty());
  }
  SECTION("a successful claim with no token yields nothing")
  {
    auto result = bridge::parse_response(200, ubus_ok(json {{"ok", true}}));
    REQUIRE(bridge::claim_token(result).empty());
  }
}

TEST_CASE("a missing transport is a named failure, not a crash", "[ubus]")
{
  bridge::ubus_client client("http://192.168.8.1", "tok-abc123");

  auto result = client.get_config();

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error_code == "no_transport");
}
