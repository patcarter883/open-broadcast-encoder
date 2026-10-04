// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// RFC 8628 device authorization. Lives in the test tree rather than requiring a
// live backplane: the transport is scripted, so the wire contract AND the
// polling rules are asserted rather than assumed.

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

#include "backplane/device_auth.h"
#include "lib/lib.h"

namespace
{
// A transport driven by a canned script, recording what was actually sent.
struct scripted_transport
{
  struct reply
  {
    int status = 200;
    std::string body;
  };

  std::vector<reply> replies;
  std::vector<std::string> paths;
  std::vector<std::string> bodies;
  std::vector<std::string> tokens;  // the bearer header value, per call
  size_t next = 0;

  backplane::device_auth::transport_fn fn()
  {
    return [this](const std::string&, const std::string& path,
                  const std::string& token,
                  const std::string& body) -> std::pair<int, std::string>
    {
      paths.push_back(path);
      bodies.push_back(body);
      tokens.push_back(token);
      if (next >= replies.size())
      {
        return {0, ""};  // script exhausted: behave like a dead server
      }
      const reply r = replies[next++];
      return {r.status, r.body};
    };
  }
};

const char* k_code_body =
    R"({"device_code":"dev-code-secret-abcdef","user_code":"BCDF-GHJK",)"
    R"("verification_uri":"https://panel.example.au/link",)"
    R"("expires_in":600,"interval":5})";

std::string token_body(const std::string& token, long id)
{
  return std::string("{\"access_token\":\"") + token +
         "\",\"token_type\":\"Bearer\",\"device_id\":" +
         std::to_string(id) + "}";
}
}  // namespace

TEST_CASE("start parses the code and the polling hints the server chose",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({200, k_code_body});
  backplane::device_auth auth("https://api.example.au", t.fn());

  std::string error;
  const auto code = auth.start("editor-01", "linux", error);

  REQUIRE(error.empty());
  REQUIRE(code.valid());
  REQUIRE(code.user_code == "BCDF-GHJK");
  REQUIRE(code.verification_uri == "https://panel.example.au/link");
  REQUIRE(code.expires_in == 600);
  // The server's interval is the one that matters -- it is what the poll loop
  // must respect, so it is taken from the response, not hardcoded.
  REQUIRE(code.interval == 5);
  REQUIRE(t.paths.at(0) == "/api/v1/auth/device/code");
}

TEST_CASE("start sends name and platform so the panel can show the operator",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({200, k_code_body});
  backplane::device_auth auth("https://api.example.au", t.fn());

  std::string error;
  (void)auth.start("editor-01", "linux", error);

  const auto body = nlohmann::json::parse(t.bodies.at(0));
  REQUIRE(body.at("name") == "editor-01");
  REQUIRE(body.at("platform") == "linux");
}

TEST_CASE("start registers the device_code as a secret, because it mints the token",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({200, k_code_body});
  backplane::device_auth auth("https://api.example.au", t.fn());

  std::string error;
  const auto code = auth.start("editor-01", "linux", error);
  REQUIRE(code.valid());

  // A log line carrying the code must not print it (H2).
  const std::string redacted = secrets::redact("polling with " + code.device_code);
  REQUIRE(redacted.find(code.device_code) == std::string::npos);

  // The user_code is NOT a secret -- the operator has to read it out.
  REQUIRE(secrets::redact(code.user_code).find(code.user_code) !=
          std::string::npos);
}

TEST_CASE("start reports a transport failure instead of an empty code",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({0, ""});
  backplane::device_auth auth("https://api.example.au", t.fn());

  std::string error;
  const auto code = auth.start("editor-01", "linux", error);

  REQUIRE_FALSE(code.valid());
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("start refuses a code that is missing its parts", "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({200, R"({"user_code":"BCDF-GHJK"})"});
  backplane::device_auth auth("https://api.example.au", t.fn());

  std::string error;
  const auto code = auth.start("editor-01", "linux", error);

  REQUIRE_FALSE(code.valid());
  // The half-populated code must not be handed back as if it were usable.
  REQUIRE(code.device_code.empty());
  REQUIRE(code.user_code.empty());
}

TEST_CASE("start requires a configured backplane", "[device_auth]")
{
  backplane::device_auth auth("");
  std::string error;
  const auto code = auth.start("editor-01", "linux", error);
  REQUIRE_FALSE(code.valid());
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("poll returns the device token on approval", "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({200, token_body("device-token-xyz", 41)});
  backplane::device_auth auth("https://api.example.au", t.fn());

  backplane::device_code code;
  code.device_code = "dev-code-secret-abcdef";
  code.interval = 5;

  const auto outcome = auth.poll(code);

  REQUIRE(outcome.approved());
  REQUIRE(outcome.token == "device-token-xyz");
  REQUIRE(outcome.device_id == 41);
  // The device_code travels in the BODY: the auth endpoints take no bearer.
  REQUIRE(t.tokens.at(0).empty());
  const auto body = nlohmann::json::parse(t.bodies.at(0));
  REQUIRE(body.at("device_code") == "dev-code-secret-abcdef");
}

TEST_CASE("the issued device token is registered as a secret", "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({200, token_body("device-token-xyz", 41)});
  backplane::device_auth auth("https://api.example.au", t.fn());

  backplane::device_code code;
  code.device_code = "dev-code-secret-abcdef";
  const auto outcome = auth.poll(code);
  REQUIRE(outcome.approved());

  REQUIRE(secrets::redact("bearer " + outcome.token).find(outcome.token) ==
          std::string::npos);
}

TEST_CASE("an approval carrying no token is a failure, not a success",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({200, R"({"token_type":"Bearer","device_id":41})"});
  backplane::device_auth auth("https://api.example.au", t.fn());

  backplane::device_code code;
  code.device_code = "dev-code-secret-abcdef";
  const auto outcome = auth.poll(code);

  // The token is shown exactly once, so an approval without one has no second
  // chance -- treating it as success would strand the device.
  REQUIRE_FALSE(outcome.approved());
  REQUIRE(outcome.state == backplane::auth_state::error);
  REQUIRE_FALSE(outcome.error.empty());
}

TEST_CASE("authorization_pending is the normal waiting state, not an error",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({400, R"({"error":"authorization_pending"})"});
  backplane::device_auth auth("https://api.example.au", t.fn());

  backplane::device_code code;
  code.device_code = "dev-code-secret-abcdef";
  const auto outcome = auth.poll(code);

  REQUIRE(outcome.state == backplane::auth_state::pending);
  REQUIRE(outcome.polling());
  REQUIRE(outcome.error.empty());
}

TEST_CASE("slow_down RAISES the interval by adopting the server's value",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({400, R"({"error":"slow_down","interval":15})"});
  backplane::device_auth auth("https://api.example.au", t.fn());

  backplane::device_code code;
  code.device_code = "dev-code-secret-abcdef";
  code.interval = 5;

  const auto outcome = auth.poll(code);

  REQUIRE(outcome.state == backplane::auth_state::slow_down);
  REQUIRE(outcome.polling());
  // The server owns the interval; ignoring it is how a client stays in
  // slow_down forever.
  REQUIRE(code.interval == 15);
}

TEST_CASE("slow_down still slows down when the server omits the interval",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({400, R"({"error":"slow_down"})"});
  backplane::device_auth auth("https://api.example.au", t.fn());

  backplane::device_code code;
  code.device_code = "dev-code-secret-abcdef";
  code.interval = 5;

  (void)auth.poll(code);

  // RFC 8628 says add 5s; do that rather than resume at the old pace.
  REQUIRE(code.interval == 10);
}

TEST_CASE("denial and expiry are terminal and distinct", "[device_auth]")
{
  SECTION("denied")
  {
    scripted_transport t;
    t.replies.push_back({400, R"({"error":"access_denied"})"});
    backplane::device_auth auth("https://api.example.au", t.fn());
    backplane::device_code code;
    code.device_code = "dev-code-secret-abcdef";
    const auto outcome = auth.poll(code);
    REQUIRE(outcome.state == backplane::auth_state::denied);
    REQUIRE_FALSE(outcome.polling());
  }

  SECTION("expired")
  {
    scripted_transport t;
    t.replies.push_back({400, R"({"error":"expired_token"})"});
    backplane::device_auth auth("https://api.example.au", t.fn());
    backplane::device_code code;
    code.device_code = "dev-code-secret-abcdef";
    const auto outcome = auth.poll(code);
    REQUIRE(outcome.state == backplane::auth_state::expired);
    REQUIRE_FALSE(outcome.polling());
  }
}

TEST_CASE("a transport failure reads as no response, not as pending",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({0, ""});
  backplane::device_auth auth("https://api.example.au", t.fn());

  backplane::device_code code;
  code.device_code = "dev-code-secret-abcdef";
  const auto outcome = auth.poll(code);

  REQUIRE(outcome.state == backplane::auth_state::error);
  REQUIRE_FALSE(outcome.polling());
}

TEST_CASE("wait_for_approval polls until approved, waiting the interval between tries",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({400, R"({"error":"authorization_pending"})"});
  t.replies.push_back({400, R"({"error":"authorization_pending"})"});
  t.replies.push_back({200, token_body("device-token-xyz", 7)});

  backplane::device_auth auth("https://api.example.au", t.fn());
  std::vector<int> sleeps;
  auth.set_sleep([&sleeps](int s) { sleeps.push_back(s); });

  backplane::device_code code;
  code.device_code = "dev-code-secret-abcdef";
  code.interval = 5;

  const auto outcome = auth.wait_for_approval(code);

  REQUIRE(outcome.approved());
  REQUIRE(t.paths.size() == 3);
  // Waited once before each retry, and NOT after the successful poll.
  REQUIRE(sleeps == std::vector<int>{5, 5});
}

TEST_CASE("wait_for_approval waits the RAISED interval after a slow_down",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({400, R"({"error":"authorization_pending"})"});
  t.replies.push_back({400, R"({"error":"slow_down","interval":15})"});
  t.replies.push_back({200, token_body("device-token-xyz", 7)});

  backplane::device_auth auth("https://api.example.au", t.fn());
  std::vector<int> sleeps;
  auth.set_sleep([&sleeps](int s) { sleeps.push_back(s); });

  backplane::device_code code;
  code.device_code = "dev-code-secret-abcdef";
  code.interval = 5;

  const auto outcome = auth.wait_for_approval(code);

  REQUIRE(outcome.approved());
  // 5 before the first retry, then the RAISED 15 -- the whole point of
  // honouring slow_down.
  REQUIRE(sleeps == std::vector<int>{5, 15});
}

TEST_CASE("wait_for_approval stops when cancelled, without polling again",
          "[device_auth]")
{
  scripted_transport t;
  t.replies.push_back({400, R"({"error":"authorization_pending"})"});
  t.replies.push_back({200, token_body("should-never-be-used", 7)});

  backplane::device_auth auth("https://api.example.au", t.fn());
  auth.set_sleep([](int) {});

  backplane::device_code code;
  code.device_code = "dev-code-secret-abcdef";

  int polls = 0;
  const auto outcome = auth.wait_for_approval(
      code, [&polls] { return ++polls > 1; });  // cancel on the second check

  REQUIRE(outcome.state == backplane::auth_state::error);
  REQUIRE(outcome.error == "cancelled");
  // Cancelled before the second poll, so the scripted token was never taken.
  REQUIRE(t.paths.size() == 1);
}
