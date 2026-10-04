// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The orchestrator: which bridge, whether it may be claimed, and what actually gets
// sent. Discovery and ubus are both faked, so the DT-21 state machine is exercised
// without a router.

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bridge/bridge_client.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace
{
using nlohmann::json;

auto advertised(const std::string& instance,
                std::map<std::string, std::string> txt = {},
                const std::string& address = "192.168.8.1")
    -> bridge::mdns::service
{
  bridge::mdns::service service;
  service.instance = instance;
  service.host = instance + ".local";
  service.address = address;
  service.port = 5000;
  service.txt = std::move(txt);
  return service;
}

auto discovery(std::vector<bridge::mdns::service> services)
    -> bridge::bridge_client::discover_fn
{
  return [services = std::move(services)](std::chrono::milliseconds) {
    return services;
  };
}

// A bridge that answers ubus, and remembers what it was asked.
struct fake_bridge
{
  std::vector<std::string> methods;
  std::vector<std::string> tokens_seen;
  std::vector<json> bodies;

  bool claim_succeeds = true;
  bool claim_returns_no_token = false;
  bool reconcile_succeeds = true;
  bool config_read_succeeds = true;
  std::string claim_token_value = "tok-NEW";

  json config{"ok", true};

  static auto reply(const json& payload) -> std::string
  {
    return json{{"jsonrpc", "2.0"},
                {"id", 1},
                {"result", json::array({0, payload})}}
        .dump();
  }

  auto transport() -> bridge::ubus_client::transport_fn
  {
    return [this](const std::string& url,
                  const std::string& body) -> std::pair<int, std::string> {
      const auto request = json::parse(body);
      const std::string method = request["params"][2];
      const auto args = request["params"][3];
      methods.push_back(method);
      tokens_seen.push_back(args.value("token", std::string{}));
      bodies.push_back(request);

      if (method == "claim") {
        if (!claim_succeeds) {
          return {200, reply(json{{"ok", false}, {"error", "already_claimed"},
                                  {"message", "the bridge is already claimed"}})};
        }
        const std::string token =
            claim_returns_no_token ? std::string{} : claim_token_value;
        return {200, reply(json{{"ok", true}, {"token", token}})};
      }
      if (method == "reconcile") {
        if (!reconcile_succeeds) {
          return {200, reply(json{{"ok", false}, {"error", "unmanaged"},
                                  {"message", "the bridge is not managed"}})};
        }
        return {200, reply(json{{"ok", true}})};
      }
      if (method == "get_config") {
        if (!config_read_succeeds) {
          return {0, ""};
        }
        return {200, reply(config)};
      }
      return {200, reply(json{{"ok", true}})};
    };
  }
};

auto factory(fake_bridge& fake) -> bridge::bridge_client::ubus_factory_fn
{
  return [&fake](const std::string& base, const std::string& token) {
    return std::make_unique<bridge::ubus_client>(base, token, fake.transport());
  };
}

auto count_of(const std::vector<std::string>& haystack, const std::string& needle)
    -> int
{
  int total = 0;
  for (const auto& item : haystack) {
    if (item == needle) {
      ++total;
    }
  }
  return total;
}

constexpr const char* k_virgin = "rist2rist-aa:bb";
}  // namespace

// ---------------------------------------------------------------- which bridge

TEST_CASE("a named bridge is found by its instance", "[bridge_client]")
{
  const auto result =
      bridge::find_bridge({advertised("rist2rist-aa:bb"), advertised("rist2rist-cc:dd")},
                          bridge::reconcile_request{.bridge_uid = "rist2rist-cc:dd"});

  REQUIRE(result.found);
  REQUIRE(result.service.instance == "rist2rist-cc:dd");
}

TEST_CASE("the instance match ignores case, as DNS does", "[bridge_client]")
{
  const auto result = bridge::find_bridge(
      {advertised("rist2rist-AA:BB")},
      bridge::reconcile_request{.bridge_uid = "RIST2RIST-aa:bb"});

  REQUIRE(result.found);
}

TEST_CASE("a named bridge that is absent is reported, not substituted",
          "[bridge_client]")
{
  // The dangerous failure would be quietly driving a DIFFERENT bridge.
  const auto result = bridge::find_bridge(
      {advertised("rist2rist-aa:bb")},
      bridge::reconcile_request{.bridge_uid = "rist2rist-zz:99"});

  REQUIRE_FALSE(result.found);
  REQUIRE(result.error_code == "bridge_not_found");
}

TEST_CASE("with no name, a lone bridge is taken", "[bridge_client]")
{
  const auto result = bridge::find_bridge(
      {advertised(k_virgin)}, bridge::reconcile_request{});

  REQUIRE(result.found);
  REQUIRE(result.service.instance == k_virgin);
}

TEST_CASE("with no name, two bridges is ambiguous rather than a guess",
          "[bridge_client]")
{
  const auto result = bridge::find_bridge(
      {advertised("rist2rist-aa:bb"), advertised("rist2rist-cc:dd")},
      bridge::reconcile_request{});

  REQUIRE_FALSE(result.found);
  REQUIRE(result.error_code == "ambiguous_bridge");
}

TEST_CASE("with no name and nothing found, the browse result is reported",
          "[bridge_client]")
{
  const auto result = bridge::find_bridge({}, bridge::reconcile_request{});

  REQUIRE_FALSE(result.found);
  REQUIRE(result.error_code == "bridge_not_found");
}

// ----------------------------------------------------------------- the endpoint

TEST_CASE("the ubus endpoint uses the address and the web port", "[bridge_client]")
{
  SECTION("default port")
  {
    REQUIRE(bridge::ubus_base_url(advertised(k_virgin)) ==
            "http://192.168.8.1");
  }
  SECTION("an explicit port from TXT")
  {
    REQUIRE(bridge::ubus_base_url(advertised(k_virgin, {{"api_port", "8080"}})) ==
            "http://192.168.8.1:8080");
  }
  SECTION("80 is not spelled out")
  {
    REQUIRE(bridge::ubus_base_url(advertised(k_virgin, {{"api_port", "80"}})) ==
            "http://192.168.8.1");
  }
  SECTION("the SRV host stands in when there is no address yet")
  {
    auto service = advertised(k_virgin);
    service.address.clear();
    REQUIRE(bridge::ubus_base_url(service) == "http://rist2rist-aa:bb.local");
  }
  SECTION("nothing to reach")
  {
    bridge::mdns::service service;
    REQUIRE(bridge::ubus_base_url(service).empty());
  }
}

// --------------------------------------------------------------- the decision

TEST_CASE("a virgin bridge with no token held may be claimed", "[bridge_client]")
{
  const auto decision = bridge::decide(
      advertised(k_virgin), bridge::reconcile_request{.known_token = ""});

  REQUIRE(decision.ok);
  REQUIRE(decision.action == bridge::bridge_action::claim);
  REQUIRE(decision.token.empty());
}

TEST_CASE("claiming can be refused by the caller", "[bridge_client]")
{
  const auto decision = bridge::decide(
      advertised(k_virgin),
      bridge::reconcile_request{.known_token = "", .allow_claim = false});

  REQUIRE_FALSE(decision.ok);
  REQUIRE(decision.error_code == "not_claimed");
}

TEST_CASE("a bridge that already advertises a fingerprint is NOT claimable",
          "[bridge_client]")
{
  // Somebody owns it. Claiming would take it from them, and it would fail anyway.
  const auto decision = bridge::decide(
      advertised(k_virgin, {{"fingerprint", "ab12cd34"}}),
      bridge::reconcile_request{.known_token = ""});

  REQUIRE_FALSE(decision.ok);
  REQUIRE(decision.error_code == "already_claimed");
}

TEST_CASE("a bridge that says it is claimed is NOT claimable", "[bridge_client]")
{
  const auto decision = bridge::decide(
      advertised(k_virgin, {{"claimed", "1"}}),
      bridge::reconcile_request{.known_token = ""});

  REQUIRE_FALSE(decision.ok);
  REQUIRE(decision.error_code == "already_claimed");
}

TEST_CASE("a held token with a matching fingerprint applies", "[bridge_client]")
{
  const auto decision = bridge::decide(
      advertised(k_virgin, {{"fingerprint", "ab12cd34"}}),
      bridge::reconcile_request{.fingerprint = "ab12cd34",
                                .known_token = "tok-1"});

  REQUIRE(decision.ok);
  REQUIRE(decision.action == bridge::bridge_action::apply);
  REQUIRE(decision.token == "tok-1");
}

TEST_CASE("a reset bridge is named as reset, not as a generic failure",
          "[bridge_client]")
{
  // The factory-reset case: it lives, it answers, but it is not the box the portal
  // registered any more.
  const auto decision = bridge::decide(
      advertised(k_virgin, {{"fingerprint", "ffffffff"}}),
      bridge::reconcile_request{.fingerprint = "ab12cd34",
                                .known_token = "tok-1"});

  REQUIRE_FALSE(decision.ok);
  REQUIRE(decision.error_code == "fingerprint_mismatch");
  REQUIRE(decision.error.find("reset") != std::string::npos);
}

TEST_CASE("a held token still applies when the bridge publishes no fingerprint",
          "[bridge_client]")
{
  // An older advertisement may omit TXT entirely; the token is still required by the
  // bridge, so applying is right -- it just cannot be verified by fingerprint.
  const auto decision = bridge::decide(
      advertised(k_virgin),
      bridge::reconcile_request{.fingerprint = "ab12cd34",
                                .known_token = "tok-1"});

  REQUIRE(decision.ok);
  REQUIRE(decision.action == bridge::bridge_action::apply);
}

// -------------------------------------------------------------- the reconcile

TEST_CASE("a virgin bridge is claimed, then configured with the new token",
          "[bridge_client]")
{
  fake_bridge fake;
  bridge::bridge_client client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin,
                                .desired = json{{"listen_url", "rist://0.0.0.0:6000"}}},
      std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(outcome.action == bridge::bridge_action::claim);
  REQUIRE(outcome.new_token == "tok-NEW");
  REQUIRE(count_of(fake.methods, "claim") == 1);
  REQUIRE(count_of(fake.methods, "reconcile") == 1);
  // The claim went out with no token; the apply went out with the new one.
  REQUIRE(fake.tokens_seen[0].empty());
  REQUIRE(fake.tokens_seen[1] == "tok-NEW");
  REQUIRE(outcome.report.bridge_uid == k_virgin);
}

TEST_CASE("an empty desired config is never applied, so outputs are not wiped",
          "[bridge_client]")
{
  // reconcile replaces outputs WHOLESALE: sending an empty config to a working
  // bridge would strip its destinations.
  fake_bridge fake;
  bridge::bridge_client client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin,
                                .known_token = "tok-1", .desired = json::object()},
      std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(outcome.action == bridge::bridge_action::apply);
  REQUIRE(count_of(fake.methods, "reconcile") == 0);
  // It still read the state back.
  REQUIRE(count_of(fake.methods, "get_config") == 1);
}

TEST_CASE("a held token means no claim is attempted", "[bridge_client]")
{
  fake_bridge fake;
  bridge::bridge_client client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin, .known_token = "tok-1",
                                .desired = json{{"listen_url", "rist://a:1"}}},
      std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(count_of(fake.methods, "claim") == 0);
  REQUIRE(fake.tokens_seen[0] == "tok-1");
  REQUIRE(outcome.new_token.empty());
}

TEST_CASE("the report carries what the bridge says, not what we asked for",
          "[bridge_client]")
{
  fake_bridge fake;
  fake.config = json{{"ok", true}, {"listen_url", "rist://0.0.0.0:6000"},
                     {"managed", true}, {"running", true}};
  bridge::bridge_client client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin, .known_token = "tok-1",
                                .desired = json{{"listen_url", "rist://b:2"}}},
      std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(outcome.report.reported_config["running"] == true);
  REQUIRE(outcome.report.managed);
  REQUIRE(outcome.report.api_version.empty());
}

TEST_CASE("the TXT state is reported before anything is attempted",
          "[bridge_client]")
{
  fake_bridge fake;
  bridge::bridge_client client(
      discovery({advertised(k_virgin, {{"api", "1"}, {"managed", "1"}})}),
      factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin},
      std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(outcome.report.api_version == "1");
  REQUIRE(outcome.report.managed);
}

TEST_CASE("a refused claim is surfaced with the bridge's own reason",
          "[bridge_client]")
{
  fake_bridge fake;
  fake.claim_succeeds = false;
  bridge::bridge_client client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin},
      std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error_code == "already_claimed");
  REQUIRE(outcome.report.last_error == "the bridge is already claimed");
  REQUIRE(count_of(fake.methods, "reconcile") == 0);
}

TEST_CASE("a claim that returns no token is a failure, not a success",
          "[bridge_client]")
{
  // The bridge stores only a hash, so a lost plaintext can never be recovered.
  fake_bridge fake;
  fake.claim_returns_no_token = true;
  bridge::bridge_client client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin},
      std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error_code == "no_token");
  REQUIRE(outcome.new_token.empty());
}

TEST_CASE("a refused apply is surfaced, and the read-back is skipped",
          "[bridge_client]")
{
  fake_bridge fake;
  fake.reconcile_succeeds = false;
  bridge::bridge_client client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin, .known_token = "tok-1",
                                .desired = json{{"listen_url", "rist://a:1"}}},
      std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error_code == "unmanaged");
  REQUIRE(count_of(fake.methods, "get_config") == 0);
}

TEST_CASE("an unreachable bridge names the transport failure", "[bridge_client]")
{
  // A factory that hands back a client whose transport never answers.
  bridge::bridge_client::ubus_factory_fn dead =
      [](const std::string& base, const std::string& token) {
        return std::make_unique<bridge::ubus_client>(
            base, token,
            [](const std::string&, const std::string&) {
              return std::make_pair(0, std::string{});
            });
      };

  bridge::bridge_client client(discovery({advertised(k_virgin)}), dead);
  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin},
      std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error_code == "no_response");
}

TEST_CASE("an apply still succeeds when the read-back fails", "[bridge_client]")
{
  // A failed read must not undo a write that already landed.
  fake_bridge fake;
  fake.config_read_succeeds = false;
  bridge::bridge_client client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin, .known_token = "tok-1",
                                .desired = json{{"listen_url", "rist://a:1"}}},
      std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(outcome.report.reported_config.is_null());
}

TEST_CASE("no bridge on the LAN is reported, not thrown", "[bridge_client]")
{
  fake_bridge fake;
  bridge::bridge_client client(discovery({}), factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request{.bridge_uid = k_virgin},
      std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error_code == "bridge_not_found");
  REQUIRE(fake.methods.empty());
}
