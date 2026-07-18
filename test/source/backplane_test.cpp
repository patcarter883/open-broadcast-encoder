// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// M2.7: persist-before-use + abandon-and-reallocate for the hosted backplane
// client. Pure logic (scripted transport) — no live backplane.

#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "backplane/backplane.h"

namespace
{
constexpr const char* k_good =
    "{\"ok\":true,\"session\":{\"id\":\"s_abc\",\"state\":\"allocated\","
    "\"rist_url\":\"rist://syd1-a.relay.example.au:20144\",\"psk\":\"deadbeef\","
    "\"psk_aes\":256,\"control_url\":\"https://syd1-a.relay.example.au/s/s_abc\","
    "\"control_token\":\"tok-xyz\",\"start_body\":{\"schema_version\":2,"
    "\"session_id\":\"s_abc\",\"outputs\":[]}}}";

// A scripted transport that records call order and returns a canned allocate.
auto scripted(std::vector<std::string>& calls, int alloc_status,
              std::string alloc_body) -> backplane_client::transport_fn
{
  return [&calls, alloc_status, alloc_body = std::move(alloc_body)](
             const std::string& method,
             const std::string& path,
             const std::string&,
             const std::string&) -> std::pair<int, std::string>
  {
    calls.push_back(method + " " + path);
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
  backplane_client client("https://api.example.au", "devtok",
                          scripted(calls, 201, k_good));
  client.set_persist([&](const hosted_session& s)
                     { persisted = s; persisted_flag = true; });

  const alloc_result r = client.allocate("syd1", {1, 2}, true);

  REQUIRE(r.ok);
  CHECK(persisted_flag);  // persist ran during allocate(), before it returned
  CHECK(persisted.session_id == "s_abc");
  CHECK(persisted.control_token == "tok-xyz");
  CHECK(persisted.psk == "deadbeef");
  CHECK_FALSE(persisted.start_body_json.empty());
  CHECK(r.session.session_id == "s_abc");
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

TEST_CASE("abandon-and-reallocate deletes then allocates, in order", "[backplane][m2.7]")
{
  std::vector<std::string> calls;
  int persists = 0;
  backplane_client client("https://api.example.au", "devtok",
                          scripted(calls, 201, k_good));
  client.set_persist([&](const hosted_session&) { ++persists; });

  const alloc_result r = client.abandon_and_reallocate("s_lost", "syd1", {1}, false);

  REQUIRE(r.ok);
  REQUIRE(calls.size() == 2);
  CHECK(calls[0] == "DELETE /api/v1/sessions/s_lost");
  CHECK(calls[1] == "POST /api/v1/sessions");
  CHECK(persists == 1);  // only the fresh session is persisted
}

TEST_CASE("allocation failure surfaces error and does not persist", "[backplane][m2.7]")
{
  std::vector<std::string> calls;
  bool persisted = false;
  backplane_client client(
      "https://api.example.au", "devtok",
      scripted(calls, 402,
               "{\"ok\":false,\"error_code\":\"plan_limit\",\"message\":\"limit\"}"));
  client.set_persist([&](const hosted_session&) { persisted = true; });

  const alloc_result r = client.allocate("syd1", {1}, false);

  CHECK_FALSE(r.ok);
  CHECK_FALSE(persisted);
  CHECK(r.error == "limit");
}

TEST_CASE("no response from backplane is a clean failure", "[backplane][m2.7]")
{
  auto dead = [](const std::string&, const std::string&, const std::string&,
                 const std::string&) -> std::pair<int, std::string>
  { return {0, ""}; };
  backplane_client client("https://api.example.au", "devtok", dead);

  std::string err;
  CHECK_FALSE(client.deallocate("s_x", err));
  CHECK_FALSE(client.allocate("syd1", {}, false).ok);
}
