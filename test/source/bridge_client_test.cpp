// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The orchestrator: which bridge, whether it may be claimed, and what actually
// gets sent. Discovery and ubus are both faked, so the DT-21 state machine is
// exercised without a router.

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
  return [services = std::move(services)](std::chrono::milliseconds)
  { return services; };
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

  json config {"ok", true};
  bool calibrate_succeeds = true;
  // A JSON-RPC-level error, i.e. no `result` member at all.
  bool calibrate_jsonrpc_error = false;
  // The bridge's own report, shaped exactly as the plugin emits it: a leg whose
  // shaper is null, a leg that measured, and the shaped confirmation.
  json calibrate_report {
      {"ok", true},
      {"scale", 100},
      {"aggregate_kbps", 750},
      {"aggregate_state", "ok"},
      {"shaper", "restored"},
      {"legs",
       json::array({json {{"interface", "wwan0"},
                          {"state", "ok"},
                          {"weight", 100},
                          {"measured_kbps", 750},
                          {"shaper_kbps", 675},
                          {"repeats_kbps", json::array({750, 750, 750})},
                          {"quality", 81},
                          {"shaped_measured_kbps", 505},
                          {"shaped_failed_at_kbps", 675},
                          {"shaped_verdict", "confirmed"}},
                    json {{"interface", "wwan1"},
                          {"state", "ok"},
                          {"weight", 0},
                          {"measured_kbps", 0},
                          {"shaper_kbps", nullptr},
                          {"repeats_kbps", json::array({0, 0, 0})}}})}};

  static auto reply(const json& payload) -> std::string
  {
    return json {
        {"jsonrpc", "2.0"}, {"id", 1}, {"result", json::array({0, payload})}}
        .dump();
  }

  auto transport() -> bridge::ubus_client::transport_fn
  {
    return [this](const std::string& url,
                  const std::string& body) -> std::pair<int, std::string>
    {
      const auto request = json::parse(body);
      const std::string method = request["params"][2];
      const auto args = request["params"][3];
      methods.push_back(method);
      tokens_seen.push_back(args.value("token", std::string {}));
      bodies.push_back(request);

      if (method == "claim") {
        if (!claim_succeeds) {
          return {200,
                  reply(json {{"ok", false},
                              {"error", "already_claimed"},
                              {"message", "the bridge is already claimed"}})};
        }
        const std::string token =
            claim_returns_no_token ? std::string {} : claim_token_value;
        return {200, reply(json {{"ok", true}, {"token", token}})};
      }
      if (method == "reconcile") {
        if (!reconcile_succeeds) {
          return {200,
                  reply(json {{"ok", false},
                              {"error", "unmanaged"},
                              {"message", "the bridge is not managed"}})};
        }
        return {200, reply(json {{"ok", true}})};
      }
      if (method == "calibrate") {
        if (calibrate_jsonrpc_error) {
          return {
              200,
              json {{"jsonrpc", "2.0"},
                    {"id", 1},
                    {"error",
                     json {{"code", -32601}, {"message", "unknown method"}}}}
                  .dump()};
        }
        if (!calibrate_succeeds) {
          return {
              200,
              reply(json {{"ok", false},
                          {"error", "calibration_refused"},
                          {"message", "a session is running on the bridge"}})};
        }
        return {200, reply(calibrate_report)};
      }
      if (method == "get_config") {
        if (!config_read_succeeds) {
          return {0, ""};
        }
        return {200, reply(config)};
      }
      return {200, reply(json {{"ok", true}})};
    };
  }
};

auto factory(fake_bridge& fake) -> bridge::bridge_client::ubus_factory_fn
{
  return [&fake](const std::string& base, const std::string& token)
  {
    return std::make_unique<bridge::ubus_client>(base, token, fake.transport());
  };
}

auto count_of(const std::vector<std::string>& haystack,
              const std::string& needle) -> int
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
  const auto result = bridge::find_bridge(
      {advertised("rist2rist-aa:bb"), advertised("rist2rist-cc:dd")},
      bridge::reconcile_request {.bridge_uid = "rist2rist-cc:dd"});

  REQUIRE(result.found);
  REQUIRE(result.service.instance == "rist2rist-cc:dd");
}

TEST_CASE("the instance match ignores case, as DNS does", "[bridge_client]")
{
  const auto result = bridge::find_bridge(
      {advertised("rist2rist-AA:BB")},
      bridge::reconcile_request {.bridge_uid = "RIST2RIST-aa:bb"});

  REQUIRE(result.found);
}

TEST_CASE("a named bridge that is absent is reported, not substituted",
          "[bridge_client]")
{
  // The dangerous failure would be quietly driving a DIFFERENT bridge.
  const auto result = bridge::find_bridge(
      {advertised("rist2rist-aa:bb")},
      bridge::reconcile_request {.bridge_uid = "rist2rist-zz:99"});

  REQUIRE_FALSE(result.found);
  REQUIRE(result.error_code == "bridge_not_found");
}

TEST_CASE("with no name, a lone bridge is taken", "[bridge_client]")
{
  const auto result =
      bridge::find_bridge({advertised(k_virgin)}, bridge::reconcile_request {});

  REQUIRE(result.found);
  REQUIRE(result.service.instance == k_virgin);
}

TEST_CASE("with no name, two bridges is ambiguous rather than a guess",
          "[bridge_client]")
{
  const auto result = bridge::find_bridge(
      {advertised("rist2rist-aa:bb"), advertised("rist2rist-cc:dd")},
      bridge::reconcile_request {});

  REQUIRE_FALSE(result.found);
  REQUIRE(result.error_code == "ambiguous_bridge");
}

TEST_CASE("with no name and nothing found, the browse result is reported",
          "[bridge_client]")
{
  const auto result = bridge::find_bridge({}, bridge::reconcile_request {});

  REQUIRE_FALSE(result.found);
  REQUIRE(result.error_code == "bridge_not_found");
}

// ----------------------------------------------------------------- the
// endpoint

TEST_CASE("the ubus endpoint uses the address and the web port",
          "[bridge_client]")
{
  SECTION("default port")
  {
    REQUIRE(bridge::ubus_base_url(advertised(k_virgin))
            == "http://192.168.8.1");
  }
  SECTION("an explicit port from TXT")
  {
    REQUIRE(bridge::ubus_base_url(advertised(k_virgin, {{"api_port", "8080"}}))
            == "http://192.168.8.1:8080");
  }
  SECTION("80 is not spelled out")
  {
    REQUIRE(bridge::ubus_base_url(advertised(k_virgin, {{"api_port", "80"}}))
            == "http://192.168.8.1");
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

TEST_CASE("a virgin bridge with no token held may be claimed",
          "[bridge_client]")
{
  const auto decision = bridge::decide(
      advertised(k_virgin), bridge::reconcile_request {.known_token = ""});

  REQUIRE(decision.ok);
  REQUIRE(decision.action == bridge::bridge_action::claim);
  REQUIRE(decision.token.empty());
}

TEST_CASE("claiming can be refused by the caller", "[bridge_client]")
{
  const auto decision = bridge::decide(
      advertised(k_virgin),
      bridge::reconcile_request {.known_token = "", .allow_claim = false});

  REQUIRE_FALSE(decision.ok);
  REQUIRE(decision.error_code == "not_claimed");
}

TEST_CASE("a bridge that already advertises a fingerprint is NOT claimable",
          "[bridge_client]")
{
  // Somebody owns it. Claiming would take it from them, and it would fail
  // anyway.
  const auto decision =
      bridge::decide(advertised(k_virgin, {{"fingerprint", "ab12cd34"}}),
                     bridge::reconcile_request {.known_token = ""});

  REQUIRE_FALSE(decision.ok);
  REQUIRE(decision.error_code == "already_claimed");
}

TEST_CASE("a bridge that says it is claimed is NOT claimable",
          "[bridge_client]")
{
  const auto decision =
      bridge::decide(advertised(k_virgin, {{"claimed", "1"}}),
                     bridge::reconcile_request {.known_token = ""});

  REQUIRE_FALSE(decision.ok);
  REQUIRE(decision.error_code == "already_claimed");
}

TEST_CASE("a held token with a matching fingerprint applies", "[bridge_client]")
{
  const auto decision =
      bridge::decide(advertised(k_virgin, {{"fingerprint", "ab12cd34"}}),
                     bridge::reconcile_request {.fingerprint = "ab12cd34",
                                                .known_token = "tok-1"});

  REQUIRE(decision.ok);
  REQUIRE(decision.action == bridge::bridge_action::apply);
  REQUIRE(decision.token == "tok-1");
}

TEST_CASE("a reset bridge is named as reset, not as a generic failure",
          "[bridge_client]")
{
  // The factory-reset case: it lives, it answers, but it is not the box the
  // portal registered any more.
  const auto decision =
      bridge::decide(advertised(k_virgin, {{"fingerprint", "ffffffff"}}),
                     bridge::reconcile_request {.fingerprint = "ab12cd34",
                                                .known_token = "tok-1"});

  REQUIRE_FALSE(decision.ok);
  REQUIRE(decision.error_code == "fingerprint_mismatch");
  REQUIRE(decision.error.find("reset") != std::string::npos);
}

TEST_CASE("a held token still applies when the bridge publishes no fingerprint",
          "[bridge_client]")
{
  // An older advertisement may omit TXT entirely; the token is still required
  // by the bridge, so applying is right -- it just cannot be verified by
  // fingerprint.
  const auto decision =
      bridge::decide(advertised(k_virgin),
                     bridge::reconcile_request {.fingerprint = "ab12cd34",
                                                .known_token = "tok-1"});

  REQUIRE(decision.ok);
  REQUIRE(decision.action == bridge::bridge_action::apply);
}

// -------------------------------------------------------------- the reconcile

TEST_CASE("a virgin bridge is claimed, then configured with the new token",
          "[bridge_client]")
{
  fake_bridge fake;
  bridge::bridge_client client(discovery({advertised(k_virgin)}),
                               factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request {
          .bridge_uid = k_virgin,
          .desired = json {{"listen_url", "rist://0.0.0.0:6000"}}},
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

TEST_CASE("the claim carries the ENCODER's identity, not the bridge's own name",
          "[bridge_client]")
{
  // The bridge records claimed_by from this field. Passing the bridge's own
  // mDNS instance made claimed_by a self-referential restatement of the
  // bridge's name -- a field whose whole job is to say WHICH controller claimed
  // it, filled with the wrong thing, and persisted to the router's config as if
  // it meant something.
  fake_bridge fake;
  bridge::bridge_client client(discovery({advertised(k_virgin)}),
                               factory(fake));

  const auto outcome =
      client.reconcile(bridge::reconcile_request {.bridge_uid = k_virgin,
                                                  .encoder_uid = "device-7"},
                       std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(outcome.action == bridge::bridge_action::claim);
  const auto& claim = fake.bodies.at(0);
  REQUIRE(claim["params"][2] == "claim");
  REQUIRE(claim["params"][3]["device_uid"] == "device-7");
  REQUIRE(claim["params"][3]["device_uid"] != k_virgin);
}

TEST_CASE(
    "an encoder with no identity sends an empty device_uid, not the bridge's "
    "name",
    "[bridge_client]")
{
  fake_bridge fake;
  bridge::bridge_client client(discovery({advertised(k_virgin)}),
                               factory(fake));

  const auto outcome =
      client.reconcile(bridge::reconcile_request {.bridge_uid = k_virgin},
                       std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(fake.bodies.at(0)["params"][3]["device_uid"] == "");
}

TEST_CASE("an empty desired config is never applied, so outputs are not wiped",
          "[bridge_client]")
{
  // reconcile replaces outputs WHOLESALE: sending an empty config to a working
  // bridge would strip its destinations.
  fake_bridge fake;
  bridge::bridge_client client(discovery({advertised(k_virgin)}),
                               factory(fake));

  const auto outcome =
      client.reconcile(bridge::reconcile_request {.bridge_uid = k_virgin,
                                                  .known_token = "tok-1",
                                                  .desired = json::object()},
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
  bridge::bridge_client client(discovery({advertised(k_virgin)}),
                               factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request {
          .bridge_uid = k_virgin,
          .known_token = "tok-1",
          .desired = json {{"listen_url", "rist://a:1"}}},
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
  fake.config = json {{"ok", true},
                      {"listen_url", "rist://0.0.0.0:6000"},
                      {"managed", true},
                      {"running", true}};
  bridge::bridge_client client(discovery({advertised(k_virgin)}),
                               factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request {
          .bridge_uid = k_virgin,
          .known_token = "tok-1",
          .desired = json {{"listen_url", "rist://b:2"}}},
      std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(outcome.report.reported_config["running"] == true);
  REQUIRE(outcome.report.managed);
  REQUIRE(outcome.report.api_version.empty());
}

TEST_CASE("the outcome carries the advertisement a report can be built from",
          "[bridge_client]")
{
  // The bridge report body needs the ADDRESS and the API VERSION, and both live
  // in the advertisement rather than in the bridge's config. Carrying the
  // advertisement on the outcome is what lets the caller report without a
  // second browse and without rebuilding it from the report alone (which would
  // lose both).
  fake_bridge fake;
  bridge::bridge_client client(
      discovery(
          {advertised(k_virgin, {{"api", "2"}, {"managed", "1"}}, "10.0.0.7")}),
      factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request {
          .bridge_uid = k_virgin,
          .known_token = "tok-1",
          .desired = json {{"listen_url", "rist://c:3"}}},
      std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(outcome.service.instance == k_virgin);
  REQUIRE(outcome.service.address == "10.0.0.7");
  REQUIRE(outcome.service.txt_value(bridge::k_txt_api) == "2");
}

TEST_CASE("a refused outcome still carries the advertisement",
          "[bridge_client]")
{
  // The service is recorded before the decision, so the failure path keeps it
  // too -- a report of "this bridge refused" still needs to say WHICH bridge.
  fake_bridge fake;
  bridge::bridge_client client(
      discovery(
          {advertised(k_virgin, {{"fingerprint", "ab12cd34"}}, "10.0.0.9")}),
      factory(fake));

  const auto outcome =
      client.reconcile(bridge::reconcile_request {.bridge_uid = k_virgin},
                       std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error_code == "already_claimed");
  REQUIRE(outcome.service.instance == k_virgin);
  REQUIRE(outcome.service.address == "10.0.0.9");
}

TEST_CASE("the TXT state is reported before anything is attempted",
          "[bridge_client]")
{
  fake_bridge fake;
  bridge::bridge_client client(
      discovery({advertised(k_virgin, {{"api", "1"}, {"managed", "1"}})}),
      factory(fake));

  const auto outcome =
      client.reconcile(bridge::reconcile_request {.bridge_uid = k_virgin},
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
  bridge::bridge_client client(discovery({advertised(k_virgin)}),
                               factory(fake));

  const auto outcome =
      client.reconcile(bridge::reconcile_request {.bridge_uid = k_virgin},
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
  bridge::bridge_client client(discovery({advertised(k_virgin)}),
                               factory(fake));

  const auto outcome =
      client.reconcile(bridge::reconcile_request {.bridge_uid = k_virgin},
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
  bridge::bridge_client client(discovery({advertised(k_virgin)}),
                               factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request {
          .bridge_uid = k_virgin,
          .known_token = "tok-1",
          .desired = json {{"listen_url", "rist://a:1"}}},
      std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error_code == "unmanaged");
  REQUIRE(count_of(fake.methods, "get_config") == 0);
}

TEST_CASE("an unreachable bridge names the transport failure",
          "[bridge_client]")
{
  // A factory that hands back a client whose transport never answers.
  bridge::bridge_client::ubus_factory_fn dead =
      [](const std::string& base, const std::string& token)
  {
    return std::make_unique<bridge::ubus_client>(
        base,
        token,
        [](const std::string&, const std::string&)
        { return std::make_pair(0, std::string {}); });
  };

  bridge::bridge_client client(discovery({advertised(k_virgin)}), dead);
  const auto outcome =
      client.reconcile(bridge::reconcile_request {.bridge_uid = k_virgin},
                       std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error_code == "no_response");
}

TEST_CASE("an apply still succeeds when the read-back fails", "[bridge_client]")
{
  // A failed read must not undo a write that already landed.
  fake_bridge fake;
  fake.config_read_succeeds = false;
  bridge::bridge_client client(discovery({advertised(k_virgin)}),
                               factory(fake));

  const auto outcome = client.reconcile(
      bridge::reconcile_request {
          .bridge_uid = k_virgin,
          .known_token = "tok-1",
          .desired = json {{"listen_url", "rist://a:1"}}},
      std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(outcome.report.reported_config.is_null());
}

TEST_CASE("no bridge on the LAN is reported, not thrown", "[bridge_client]")
{
  fake_bridge fake;
  bridge::bridge_client client(discovery({}), factory(fake));

  const auto outcome =
      client.reconcile(bridge::reconcile_request {.bridge_uid = k_virgin},
                       std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error_code == "bridge_not_found");
  REQUIRE(fake.methods.empty());
}

TEST_CASE("a bridge with no advertisement is never labelled virgin",
          "[bridge_client]")
{
  // The manual-address path: the operator typed an address, so the browse found
  // nothing and the address stands in with no TXT at all. An absent fingerprint
  // there is NOT evidence of a virgin bridge -- it is no evidence at all -- and
  // saying "virgin, claimable" sent the operator to Claim a bridge which
  // answers already_claimed. What we HOLD has to be decided first.
  const auto manual = advertised("rist2rist-aa:bb:cc:dd:ee:ff");

  const auto held = bridge::bridge_state_label(manual, /*holds_token=*/true);
  REQUIRE(held.find("virgin") == std::string::npos);
  REQUIRE(held.find("claimed") != std::string::npos);

  // With no token held either, the label must still not assert virginity: only
  // the bridge itself can say, and it is asked before anything is applied.
  const auto unknown =
      bridge::bridge_state_label(manual, /*holds_token=*/false);
  REQUIRE(unknown.find("virgin") == std::string::npos);
  REQUIRE(unknown.find("no advertisement") != std::string::npos);
}

TEST_CASE("an advertised fingerprint with no token held reads as another's",
          "[bridge_client]")
{
  const auto published = advertised("rist2rist-aa:bb:cc:dd:ee:ff",
                                    {{"fingerprint", "0123456789abcdef"}});

  const auto label =
      bridge::bridge_state_label(published, /*holds_token=*/false);
  REQUIRE(label.find("elsewhere") != std::string::npos);
  REQUIRE(label.find("virgin") == std::string::npos);

  // ...and OUR token over an advertised fingerprint reads as ours, which is the
  // claimed-by-us state DT-21 calls out.
  const auto mine = bridge::bridge_state_label(published, /*holds_token=*/true);
  REQUIRE(mine.find("this encoder holds") != std::string::npos);
}

// ------------------------------------------------------------------
// calibration

TEST_CASE("a calibration report is read leg by leg", "[bridge_client]")
{
  auto fake = fake_bridge {};
  auto client =
      bridge::bridge_client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome =
      client.calibrate(bridge::reconcile_request {.known_token = "tok"},
                       std::chrono::milliseconds(1));

  REQUIRE(outcome.ok);
  REQUIRE(outcome.aggregate_kbps == 750);
  REQUIRE(outcome.legs.size() == 2);
  REQUIRE(outcome.legs.at(0).interface_ == "wwan0");
  REQUIRE(outcome.legs.at(0).measured_kbps == 750);
  REQUIRE(outcome.legs.at(0).has_shaper);
  REQUIRE(outcome.legs.at(0).shaper_kbps == 675);
  REQUIRE(outcome.legs.at(0).repeats_kbps == std::vector<int> {750, 750, 750});
  REQUIRE(outcome.legs.at(0).shaped_verdict == "confirmed");
  REQUIRE(outcome.legs.at(0).shaped_failed_at_kbps == 675);
  // A leg with NO shaper must not read as a shaper of zero: the plugin sends
  // null, and "0" would look like a value the operator could trust.
  REQUIRE_FALSE(outcome.legs.at(1).has_shaper);
}

TEST_CASE("calibration is asked for by name, with the pair token",
          "[bridge_client]")
{
  auto fake = fake_bridge {};
  auto client =
      bridge::bridge_client(discovery({advertised(k_virgin)}), factory(fake));

  (void)client.calibrate(bridge::reconcile_request {.known_token = "tok-ABC"},
                         std::chrono::milliseconds(1));

  REQUIRE(count_of(fake.methods, "calibrate") == 1);
  REQUIRE(fake.tokens_seen.back() == "tok-ABC");
  // Assert what the ORCHESTRATOR sent, not merely that the ubus layer can carry
  // it: the method and token are the whole request.
  REQUIRE(fake.bodies.back()["params"][2] == "calibrate");
}

TEST_CASE(
    "without a token, calibration says what to do instead of relaying a "
    "refusal",
    "[bridge_client]")
{
  auto fake = fake_bridge {};
  auto client =
      bridge::bridge_client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome = client.calibrate(bridge::reconcile_request {},
                                        std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error.find("claim the bridge first") != std::string::npos);
  // Nothing was sent: no token, no call.
  REQUIRE(count_of(fake.methods, "calibrate") == 0);
}

TEST_CASE(
    "a mid-stream refusal is the bridge answering, not a transport failure",
    "[bridge_client]")
{
  auto fake = fake_bridge {};
  fake.calibrate_succeeds = false;
  auto client =
      bridge::bridge_client(discovery({advertised(k_virgin)}), factory(fake));

  const auto outcome =
      client.calibrate(bridge::reconcile_request {.known_token = "tok"},
                       std::chrono::milliseconds(1));

  REQUIRE_FALSE(outcome.ok);
  REQUIRE(outcome.error == "a session is running on the bridge");
}

TEST_CASE("parse_calibration keeps a transport failure distinct from a refusal",
          "[bridge_client]")
{
  // Three answers that need three different operator actions, so they must not
  // collapse into one message.
  bridge::ubus_result dead;
  dead.http_status = 0;
  REQUIRE(bridge::parse_calibration(dead).error == "the bridge did not answer");

  bridge::ubus_result denied;
  denied.http_status = 200;
  denied.ubus_status = 6;
  REQUIRE(bridge::parse_calibration(denied).error
          == "the bridge's ubus refused the call");

  bridge::ubus_result refused;
  refused.http_status = 200;
  refused.ok = false;
  refused.error = "calibration_refused";
  const auto out = bridge::parse_calibration(refused);
  REQUIRE_FALSE(out.ok);
  REQUIRE(out.error == "calibration_refused");
}

TEST_CASE("the operator line carries the numbers the decision needs",
          "[bridge_client]")
{
  auto fake = fake_bridge {};
  const auto outcome = bridge::parse_calibration(bridge::ubus_result {
      .ok = true, .http_status = 200, .data = fake.calibrate_report});

  const std::string text = bridge::format_calibration(outcome);
  REQUIRE(text.find("750 kbit/s") != std::string::npos);
  REQUIRE(text.find("[750, 750, 750]") != std::string::npos);
  REQUIRE(text.find("shaper 675") != std::string::npos);
  // The shaper sits between held and broke -- that pair IS the confirmation.
  REQUIRE(text.find("505->675") != std::string::npos);
  REQUIRE(text.find("confirmed") != std::string::npos);
  // A leg that never measured is named as such rather than shown as a zero.
  REQUIRE(text.find("wwan1") != std::string::npos);
}

TEST_CASE("the aggregate shaper field says what it is, not 'shaper none'",
          "[bridge_client]")
{
  // This field is about the shaper that was ALREADY on the bridge -- not the
  // one this run derived, which is per leg below it. Rendered raw, "shaper
  // none" read as though the derived shaper had failed to land, when it means
  // there was nothing there to put back.
  const auto render = [](const std::string& shaper)
  {
    json report = fake_bridge {}.calibrate_report;
    report["shaper"] = shaper;
    return bridge::format_calibration(bridge::parse_calibration(
        bridge::ubus_result {.ok = true, .http_status = 200, .data = report}));
  };

  // CHECK, not REQUIRE: these are four independent states, and stopping at the
  // first would leave the rest unproven in exactly the run where they matter.
  CHECK(render("none").find("nothing was shaped before") != std::string::npos);
  CHECK(render("restored").find("the previous shaper was restored")
        != std::string::npos);
  CHECK(render("restore_failed").find("PREVIOUS SHAPER NOT RESTORED")
        != std::string::npos);
  // An unknown state is shown as given rather than swallowed.
  CHECK(render("half_applied").find("half_applied") != std::string::npos);
}
