// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// M2.7: persist-before-use + abandon-and-reallocate for the hosted backplane
// client. Pure logic (scripted transport) — no live backplane.

#include <string>
#include <utility>
#include <vector>

#include "backplane/backplane.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
constexpr const char* k_good =
    "{\"ok\":true,\"session\":{\"id\":\"s_abc\",\"state\":\"allocated\","
    "\"rist_url\":\"rist://"
    "syd1-a.relay.example.au:20144\",\"psk\":\"deadbeef\","
    "\"psk_aes\":256,\"control_url\":\"https://syd1-a.relay.example.au/s/"
    "s_abc\","
    "\"control_token\":\"tok-xyz\",\"start_body\":{\"schema_version\":2,"
    "\"session_id\":\"s_abc\",\"outputs\":[]}}}";

// A scripted transport that records call order and returns a canned allocate.
// `bodies`, when given, captures each request body so a test can assert what
// the client actually SENT (not just what the response handling did).
auto scripted(std::vector<std::string>& calls,
              int alloc_status,
              std::string alloc_body,
              std::vector<std::string>* bodies = nullptr)
    -> backplane_client::transport_fn
{
  return [&calls, alloc_status, alloc_body = std::move(alloc_body), bodies](
             const std::string& method,
             const std::string& path,
             const std::string&,
             const std::string& body) -> std::pair<int, std::string>
  {
    calls.push_back(method + " " + path);
    if (bodies != nullptr) {
      bodies->push_back(body);
    }
    if (method == "POST" && path == "/api/v1/sessions") {
      return {alloc_status, alloc_body};
    }
    if (method == "DELETE") {
      return {200, "{\"ok\":true}"};
    }
    return {404, ""};
  };
}
}  // namespace

TEST_CASE("allocate persists credentials before returning", "[backplane][m2.7]")
{
  std::vector<std::string> calls;
  hosted_session persisted;
  bool persisted_flag = false;
  backplane_client client(
      "https://api.example.au", "devtok", scripted(calls, 201, k_good));
  client.set_persist(
      [&](const hosted_session& s)
      {
        persisted = s;
        persisted_flag = true;
      });

  const alloc_result r = client.allocate("syd1");

  REQUIRE(r.ok);
  CHECK(persisted_flag);  // persist ran during allocate(), before it returned
  CHECK(persisted.session_id == "s_abc");
  CHECK(persisted.control_token == "tok-xyz");
  CHECK(persisted.psk == "deadbeef");
  CHECK_FALSE(persisted.start_body_json.empty());
  CHECK(r.session.session_id == "s_abc");
}

// DT-22: the request must NOT carry transport configuration. The fan-out and
// recording are the OPERATOR's decision on the backplane; sending them from
// here would make the encoder a second source of truth, and the backplane
// refuses them outright (422) rather than silently preferring one. Assert what
// the client actually put on the wire, not merely that it can build such a
// body.
TEST_CASE("the allocate request carries no transport config",
          "[backplane][dt22]")
{
  std::vector<std::string> calls;
  std::vector<std::string> bodies;
  backplane_client client("https://api.example.au",
                          "devtok",
                          scripted(calls, 201, k_good, &bodies));

  const alloc_result r = client.allocate("syd1");

  REQUIRE(r.ok);
  REQUIRE(bodies.size() == 1);
  CHECK(bodies.at(0).find("schema_version") != std::string::npos);
  CHECK(bodies.at(0).find("syd1") != std::string::npos);
  CHECK(bodies.at(0).find("destination_ids") == std::string::npos);
  CHECK(bodies.at(0).find("record") == std::string::npos);
  CHECK(bodies.at(0).find("ingest_overrides") == std::string::npos);
}

TEST_CASE("the allocate request carries the encoder's instance name",
          "[backplane][identity]")
{
  std::vector<std::string> calls;
  std::vector<std::string> bodies;
  backplane_client client("https://api.example.au",
                          "devtok",
                          scripted(calls, 201, k_good, &bodies));

  const alloc_result r = client.allocate("syd1", "enc-studio-a");

  REQUIRE(r.ok);
  REQUIRE(bodies.size() == 1);
  CHECK(bodies.at(0).find("\"encoder_name\":\"enc-studio-a\"")
        != std::string::npos);
}

// The portal sizes a destination transcode from the source rate the encoder
// reports. Without it the rule falls back to the session's plan-clamped
// reservation, which is how a 1080p source ended up with a 29860 kbps target:
// the reservation describes the plan, not the stream.
TEST_CASE("the allocate request carries the encoder's source bitrate",
          "[backplane][bitrate]")
{
  std::vector<std::string> calls;
  std::vector<std::string> bodies;
  backplane_client client("https://api.example.au",
                          "devtok",
                          scripted(calls, 201, k_good, &bodies));

  bitrate_inputs inputs;
  inputs.source_bitrate_kbps = 4300;

  const alloc_result r = client.allocate("syd1", "enc-studio-a", inputs);

  REQUIRE(r.ok);
  REQUIRE(bodies.size() == 1);
  CHECK(bodies.at(0).find("\"source_bitrate_kbps\":4300") != std::string::npos);
}

// A WAN budget is only ever a MEASUREMENT. The bridge does not relay link
// statistics back over OOB, so the encoder has nothing to state: the field must
// be absent from the wire rather than present with a guessed number, because
// the portal cannot tell the two apart afterwards.
TEST_CASE("an unmeasured WAN budget is omitted from the allocate request",
          "[backplane][bitrate]")
{
  std::vector<std::string> calls;
  std::vector<std::string> bodies;
  backplane_client client("https://api.example.au",
                          "devtok",
                          scripted(calls, 201, k_good, &bodies));

  bitrate_inputs inputs;
  inputs.source_bitrate_kbps = 4300;

  const alloc_result r = client.allocate("syd1", "enc-studio-a", inputs);

  REQUIRE(r.ok);
  REQUIRE(bodies.size() == 1);
  CHECK(bodies.at(0).find("measured_budget_kbps") == std::string::npos);
}

// Present and real is the other half of the contract: a figure the encoder did
// measure must reach the portal, and must reach it as a number.
TEST_CASE("a measured WAN budget reaches the allocate request",
          "[backplane][bitrate]")
{
  std::vector<std::string> calls;
  std::vector<std::string> bodies;
  backplane_client client("https://api.example.au",
                          "devtok",
                          scripted(calls, 201, k_good, &bodies));

  bitrate_inputs inputs;
  inputs.source_bitrate_kbps = 6000;
  inputs.measured_budget_kbps = 9500;

  const alloc_result r = client.allocate("syd1", "enc-studio-a", inputs);

  REQUIRE(r.ok);
  REQUIRE(bodies.size() == 1);
  CHECK(bodies.at(0).find("\"source_bitrate_kbps\":6000") != std::string::npos);
  CHECK(bodies.at(0).find("\"measured_budget_kbps\":9500")
        != std::string::npos);
}

// Neither figure known: the request must still be a valid allocation, with both
// fields absent rather than zero, which the portal would read as a real rate.
TEST_CASE("an allocate request with no bitrate inputs sends neither field",
          "[backplane][bitrate]")
{
  std::vector<std::string> calls;
  std::vector<std::string> bodies;
  backplane_client client("https://api.example.au",
                          "devtok",
                          scripted(calls, 201, k_good, &bodies));

  const alloc_result r = client.allocate("syd1", "enc-studio-a");

  REQUIRE(r.ok);
  REQUIRE(bodies.size() == 1);
  CHECK(bodies.at(0).find("source_bitrate_kbps") == std::string::npos);
  CHECK(bodies.at(0).find("measured_budget_kbps") == std::string::npos);
}

TEST_CASE("hosted_session survives a JSON round trip", "[backplane][m2.7]")
{
  hosted_session s;
  s.session_id = "s_r";
  s.rist_url = "rist://h:1";
  s.control_url = "https://h/s/s_r";
  s.control_token = "ct";
  s.psk = "pk";
  s.psk_aes = 256;
  s.start_body_json = "{\"schema_version\":2,\"session_id\":\"s_r\"}";

  const hosted_session back = hosted_session::from_json(s.to_json());

  CHECK(back.session_id == "s_r");
  CHECK(back.control_token == "ct");
  CHECK(back.psk == "pk");
  CHECK(back.start_body_json.find("schema_version") != std::string::npos);
  CHECK(back.valid());
}

// DT-20.1: the chain the portal chose, and the difference between "no bridge"
// and "a backplane that never said". A version skew must not read as a routing
// decision.
TEST_CASE("the allocation carries the bridge the portal chose",
          "[backplane][dt20.1]")
{
  const std::string with_bridge =
      "{\"ok\":true,\"session\":{\"id\":\"s_abc\",\"rist_url\":\"rist://h:1\","
      "\"control_url\":\"https://h/s_abc\",\"bridge\":{\"id\":42,"
      "\"bridge_uid\":\"rist2rist-aa:bb:cc:dd:ee:ff\","
      "\"lan_host\":\"192.168.8.1\"}}}";
  {
    std::vector<std::string> calls;
    backplane_client client(
        "https://api", "devtok", scripted(calls, 201, with_bridge));
    const alloc_result r = client.allocate("syd1");
    REQUIRE(r.ok);
    CHECK(r.session.bridge_present);
    CHECK(r.session.bridge_id == 42);
    CHECK(r.session.bridge_uid == "rist2rist-aa:bb:cc:dd:ee:ff");
    CHECK(r.session.bridge_lan_host == "192.168.8.1");
  }

  const std::string explicit_null =
      "{\"ok\":true,\"session\":{\"id\":\"s_abc\",\"rist_url\":\"rist://h:1\","
      "\"control_url\":\"https://h/s_abc\",\"bridge\":null}}";
  {
    std::vector<std::string> calls;
    backplane_client client(
        "https://api", "devtok", scripted(calls, 201, explicit_null));
    const alloc_result r = client.allocate("syd1");
    REQUIRE(r.ok);
    // Present and null: the operator chose a direct chain.
    CHECK(r.session.bridge_present);
    CHECK(r.session.bridge_uid.empty());
  }

  {
    std::vector<std::string> calls;
    backplane_client client(
        "https://api", "devtok", scripted(calls, 201, k_good));
    const alloc_result r = client.allocate("syd1");
    REQUIRE(r.ok);
    // Absent: this backplane predates the field. Deliberately NOT the same
    // value as "no bridge" -- the caller must be able to tell a skew from a
    // decision.
    CHECK_FALSE(r.session.bridge_present);
  }
}

TEST_CASE("the chain survives the settings round-trip", "[backplane][dt20.1]")
{
  hosted_session s;
  s.session_id = "s_abc";
  s.rist_url = "rist://h:1";
  s.control_url = "https://h/s_abc";
  s.bridge_present = true;
  s.bridge_id = 42;
  s.bridge_uid = "rist2rist-aa:bb:cc:dd:ee:ff";
  s.bridge_lan_host = "192.168.8.1";

  const hosted_session back = hosted_session::from_json(s.to_json());

  CHECK(back.bridge_present);
  CHECK(back.bridge_id == 42);
  CHECK(back.bridge_uid == "rist2rist-aa:bb:cc:dd:ee:ff");
  CHECK(back.bridge_lan_host == "192.168.8.1");
}

TEST_CASE("abandon-and-reallocate deletes then allocates, in order",
          "[backplane][m2.7]")
{
  std::vector<std::string> calls;
  int persists = 0;
  backplane_client client(
      "https://api.example.au", "devtok", scripted(calls, 201, k_good));
  client.set_persist([&](const hosted_session&) { ++persists; });

  const alloc_result r = client.abandon_and_reallocate("s_lost", "syd1");

  REQUIRE(r.ok);
  REQUIRE(calls.size() == 2);
  CHECK(calls[0] == "DELETE /api/v1/sessions/s_lost");
  CHECK(calls[1] == "POST /api/v1/sessions");
  CHECK(persists == 1);  // only the fresh session is persisted
}

TEST_CASE("allocation failure surfaces error and does not persist",
          "[backplane][m2.7]")
{
  std::vector<std::string> calls;
  bool persisted = false;
  backplane_client client("https://api.example.au",
                          "devtok",
                          scripted(calls,
                                   402,
                                   "{\"ok\":false,\"error_code\":\"plan_"
                                   "limit\",\"message\":\"limit\"}"));
  client.set_persist([&](const hosted_session&) { persisted = true; });

  const alloc_result r = client.allocate("syd1");

  CHECK_FALSE(r.ok);
  CHECK_FALSE(persisted);
  CHECK(r.error == "limit");
}

TEST_CASE("no response from backplane is a clean failure", "[backplane][m2.7]")
{
  auto dead = [](const std::string&,
                 const std::string&,
                 const std::string&,
                 const std::string&) -> std::pair<int, std::string> {
    return {0, ""};
  };
  backplane_client client("https://api.example.au", "devtok", dead);

  std::string err;
  CHECK_FALSE(client.deallocate("s_x", err));
  CHECK_FALSE(client.allocate("syd1").ok);
}

// MT5.3: the operator sees the portal's per-destination transcode target before
// streaming. Read-only -- the encoder must never become a second place where
// the fan-out or its transcode is configured (DT-22), only somewhere it is
// visible.
TEST_CASE("the destination list shows each destination's transcode target",
          "[backplane][mt5.3]")
{
  std::vector<std::string> calls;
  const std::string body =
      R"({"ok":true,"schema_version":3,"destinations":[)"
      R"({"id":1,"label":"YT","type":"rtmp","transcode":"h264","url":"rtmp://a"},)"
      R"({"id":2,"label":"Twitch","type":"srt","transcode":null,"url":"srt://b"},)"
      R"({"id":3,"label":"Lan","type":"rist"}]})";

  backplane_client c(
      "https://api.example.au",
      "dev-tok",
      [&calls, body](const std::string& method,
                     const std::string& path,
                     const std::string&,
                     const std::string&) -> std::pair<int, std::string>
      {
        calls.push_back(method + " " + path);
        return {200, body};
      });

  const auto r = c.list_destinations();
  REQUIRE(r.ok);
  REQUIRE(r.destinations.size() == 3);
  CHECK(r.destinations[0].label == "YT");
  CHECK(r.destinations[0].type == "rtmp");
  CHECK(r.destinations[0].transcode == "h264");
  // an explicit null and an absent key both mean copy, not "h264"
  CHECK(r.destinations[1].transcode.empty());
  CHECK(r.destinations[2].transcode.empty());
  // a read must be a GET: sent as a POST the backplane answers 405
  CHECK(calls == std::vector<std::string> {"GET /api/v1/destinations"});
}

TEST_CASE("a destination list failure is non-fatal", "[backplane][mt5.3]")
{
  backplane_client c("https://api.example.au",
                     "dev-tok",
                     [](const std::string&,
                        const std::string&,
                        const std::string&,
                        const std::string&) -> std::pair<int, std::string> {
                       return {500, R"({"message":"server exploded"})"};
                     });

  const auto r = c.list_destinations();
  CHECK_FALSE(r.ok);
  CHECK(r.destinations.empty());
  CHECK(r.error == "server exploded");
}

// MT5.3: the exact wording the operator reads before allocating. Pinned because
// a silent wording change here is invisible until someone is mid-incident.
TEST_CASE("the destination list reads as what the host will do",
          "[backplane][mt5.3]")
{
  using dv = backplane_client::destination_view;
  const std::string text =
      format_destination_list({dv {"YT", "rtmp", "h264"},
                               dv {"Twitch", "srt", std::string {}},
                               dv {"Feed", "rist", "h265"}});

  CHECK(text.find("YT (rtmp) -> transcode H.264") != std::string::npos);
  CHECK(text.find("Twitch (srt) -> copy") != std::string::npos);
  CHECK(text.find("Feed (rist) -> transcode H.265") != std::string::npos);
}

TEST_CASE("an empty destination list says so", "[backplane][mt5.3]")
{
  const std::string text = format_destination_list({});
  CHECK(text.find("no saved destinations") != std::string::npos);
}

TEST_CASE("a transcode target this build does not word for is still shown",
          "[backplane][mt5.3]")
{
  using dv = backplane_client::destination_view;
  const std::string text =
      format_destination_list({dv {"Future", "rtmp", "av1"}});
  CHECK(text.find("transcode av1") != std::string::npos);
}

// ---------------------------------------------------------------------------
// E3: reattach on launch.
//
// The decision table is pure so every branch is pinned: which answers mean
// "restore and re-post", which mean "the session is over", and -- the one that
// costs money if it is wrong -- which mean "keep the record".
// ---------------------------------------------------------------------------
TEST_CASE("a fetch is classified by what it can be acted on", "[backplane][e3]")
{
  // A 2xx with a readable session is a session.
  CHECK(classify_probe(200, true) == session_probe::found);
  // A 2xx nobody can read is not "gone": treat it as a transient failure, or
  // one unparseable response would delete a live allocation.
  CHECK(classify_probe(200, false) == session_probe::unavailable);
  CHECK(classify_probe(404, false) == session_probe::gone);
  CHECK(classify_probe(401, false) == session_probe::unauthorised);
  CHECK(classify_probe(403, false) == session_probe::unauthorised);
  CHECK(classify_probe(500, false) == session_probe::unavailable);
  CHECK(classify_probe(503, false) == session_probe::unavailable);
  // No HTTP status at all: the transport never reached the portal.
  CHECK(classify_probe(0, false) == session_probe::unavailable);
}

TEST_CASE("a live session is reattached to, not replaced", "[backplane][e3]")
{
  CHECK(decide_reattach(session_probe::found, "allocated")
        == reattach_action::reattach);
  CHECK(decide_reattach(session_probe::found, "running")
        == reattach_action::reattach);
  // The portal's casing and padding are not a contract this build should
  // depend on.
  CHECK(decide_reattach(session_probe::found, " Running ")
        == reattach_action::reattach);
}

TEST_CASE("a session that is over is cleared", "[backplane][e3]")
{
  for (const char* state :
       {"ended", "released", "terminated", "completed", "expired", "closed"})
  {
    INFO("state: " << state);
    CHECK(decide_reattach(session_probe::found, state)
          == reattach_action::clear_session);
  }
  // 404 is the same fact stated differently: the portal does not have it.
  CHECK(decide_reattach(session_probe::gone, "")
        == reattach_action::clear_session);
}

TEST_CASE("an unreadable or unknown session keeps its record",
          "[backplane][e3]")
{
  // A transient failure must never delete the file: the session may be live and
  // billable, and the next launch is another chance.
  CHECK(decide_reattach(session_probe::unavailable, "")
        == reattach_action::retry);
  CHECK(decide_reattach(session_probe::unavailable, "running")
        == reattach_action::retry);

  // A refused device token will not fix itself in fifteen seconds.
  CHECK(decide_reattach(session_probe::unauthorised, "running")
        == reattach_action::keep_report);

  // A state this build does not know: neither reattach (it might be tearing
  // down) nor delete (it might be billing).
  CHECK(decide_reattach(session_probe::found, "suspended")
        == reattach_action::keep_report);

  // A document with no state word is a session the portal still has, saying
  // nothing about it being over: refusing to reattach would cost the operator
  // the destination for the rest of a long session.
  CHECK(decide_reattach(session_probe::found, "") == reattach_action::reattach);
}

TEST_CASE("a session read never blanks a show-once secret", "[backplane][e3]")
{
  hosted_session persisted;
  persisted.session_id = "s_abc";
  persisted.rist_url = "rist://syd1-a.relay.example.au:20144";
  persisted.control_url = "https://syd1-a.relay.example.au/s/s_abc";
  persisted.control_token = "tok-xyz";
  persisted.psk = "deadbeef";
  persisted.psk_aes = 256;
  persisted.start_body_json = "{\"session_id\":\"s_abc\"}";
  persisted.bridge_present = true;
  persisted.bridge_id = 7;

  // The portal restates what it may, and states the new state. It does NOT
  // restate the control token: that is shown once, at allocation.
  hosted_session fresh;
  fresh.session_id = "s_abc";
  fresh.control_url = "https://syd1-b.relay.example.au/s/s_abc";
  fresh.state = "running";

  const hosted_session merged = merge_session(persisted, fresh);

  CHECK(merged.control_url == "https://syd1-b.relay.example.au/s/s_abc");
  CHECK(merged.state == "running");
  CHECK(merged.control_token == "tok-xyz");  // not blanked
  CHECK(merged.psk == "deadbeef");
  CHECK(merged.rist_url == persisted.rist_url);  // restated by nobody
  CHECK(merged.bridge_present);
  CHECK(merged.bridge_id == 7);
}

TEST_CASE("a session is read back from the portal by id", "[backplane][e3]")
{
  std::vector<std::string> calls;
  const std::string body =
      "{\"session\":{\"id\":\"s_abc\",\"state\":\"allocated\","
      "\"rist_url\":\"rist://syd1-a.relay.example.au:20144\","
      "\"control_url\":\"https://syd1-a.relay.example.au/s/s_abc\"}}";

  backplane_client client(
      "https://api.example.au",
      "devtok",
      [&calls, &body](const std::string& method,
                      const std::string& path,
                      const std::string&,
                      const std::string&) -> std::pair<int, std::string>
      {
        calls.push_back(method + " " + path);
        return {200, body};
      });

  const session_fetch_result r = client.get_session("s_abc");

  REQUIRE(r.ok);
  CHECK(r.probe == session_probe::found);
  CHECK(r.state == "allocated");
  CHECK(r.session.session_id == "s_abc");
  CHECK(r.session.control_url == "https://syd1-a.relay.example.au/s/s_abc");
  REQUIRE(calls.size() == 1);
  CHECK(calls.at(0) == "GET /api/v1/sessions/s_abc");
}

TEST_CASE("a 404 session read is 'gone', not an error", "[backplane][e3]")
{
  backplane_client client("https://api.example.au",
                          "devtok",
                          [](const std::string&,
                             const std::string&,
                             const std::string&,
                             const std::string&) -> std::pair<int, std::string>
                          {
                            return {404, "{\"message\":\"Not found\"}"};
                          });

  const session_fetch_result r = client.get_session("s_old");

  CHECK_FALSE(r.ok);
  CHECK(r.probe == session_probe::gone);
  CHECK_FALSE(r.error.empty());
  CHECK(decide_reattach(r.probe, r.state) == reattach_action::clear_session);
}

TEST_CASE("an unreachable portal is a retry, not a verdict", "[backplane][e3]")
{
  backplane_client client("https://api.example.au",
                          "devtok",
                          [](const std::string&,
                             const std::string&,
                             const std::string&,
                             const std::string&) -> std::pair<int, std::string>
                          {
                            return {0, ""};
                          });  // transport failure

  const session_fetch_result r = client.get_session("s_abc");

  CHECK_FALSE(r.ok);
  CHECK(r.probe == session_probe::unavailable);
  CHECK(decide_reattach(r.probe, r.state) == reattach_action::retry);
}
