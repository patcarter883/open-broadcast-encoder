// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <chrono>
#include <thread>

#include "backplane/device_auth.h"

#include <nlohmann/json.hpp>

#include "lib/lib.h"

namespace backplane
{
namespace
{
constexpr const char* k_code_path = "/api/v1/auth/device/code";
constexpr const char* k_token_path = "/api/v1/auth/device/token";

// The default sleeper. Kept as a free function so the test can swap in a
// counting no-op and assert the RFC pacing without spending the time.
void real_sleep(int seconds)
{
  if (seconds > 0) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
  }
}
}  // namespace

device_auth::device_auth(std::string base_url, transport_fn transport)
    : m_base(std::move(base_url))
    , m_transport(std::move(transport))
    , m_sleep(&real_sleep)
{
}

device_code device_auth::start(const std::string& name,
                               const std::string& platform,
                               std::string& error)
{
  device_code out;
  if (m_base.empty()) {
    error = "no backplane configured";
    return out;
  }

  nlohmann::json body = nlohmann::json::object();
  if (!name.empty()) {
    body["name"] = name;
  }
  if (!platform.empty()) {
    body["platform"] = platform;
  }

  const auto [status, response] =
      m_transport("POST", k_code_path, std::string {}, body.dump());

  if (status != 200) {
    error = status == 0
        ? std::string("no response from the backplane")
        : std::string("the backplane returned HTTP ") + std::to_string(status);
    return out;
  }

  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(response);
  } catch (const nlohmann::json::exception&) {
    error = "the backplane did not return JSON";
    return out;
  }

  out.device_code = parsed.value("device_code", std::string {});
  out.user_code = parsed.value("user_code", std::string {});
  out.verification_uri = parsed.value("verification_uri", std::string {});
  out.expires_in = parsed.value("expires_in", 600);
  out.interval = parsed.value("interval", 5);

  if (!out.valid()) {
    out.device_code.clear();
    out.user_code.clear();
    error = "the backplane returned an incomplete device code";
    return out;
  }

  // The device_code mints the token, so it is a credential in its own right:
  // register it before anything can log it (H2). The user_code is deliberately
  // NOT a secret -- the operator has to read it out.
  secrets::register_secret(out.device_code);
  return out;
}

auth_outcome device_auth::poll(device_code& code)
{
  auth_outcome outcome;
  if (code.device_code.empty()) {
    outcome.error = "no device code to poll";
    return outcome;
  }

  nlohmann::json body = {{"device_code", code.device_code}};
  const auto [status, response] =
      m_transport("POST", k_token_path, std::string {}, body.dump());
  outcome.http_status = status;

  if (status == 0) {
    outcome.error = "no response from the backplane";
    return outcome;
  }

  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(response);
  } catch (const nlohmann::json::exception&) {
    outcome.error = "the backplane did not return JSON";
    return outcome;
  }

  if (status == 200) {
    outcome.token = parsed.value("access_token", std::string {});
    outcome.device_id = parsed.value("device_id", 0L);
    if (outcome.token.empty()) {
      // An approval that carries no token is a failure, not a success: the
      // token is shown once, so there is no second chance to collect it.
      outcome.state = auth_state::error;
      outcome.error = "the backplane approved the device but returned no token";
      return outcome;
    }
    outcome.state = auth_state::approved;
    secrets::register_secret(outcome.token);
    return outcome;
  }

  // A non-200 carries the RFC's error code in `error` (Laravel puts validation
  // failures in `message`; these endpoints use `error`).
  const std::string reason = parsed.contains("error")
      ? parsed.value("error", std::string {})
      : parsed.value("message", std::string {});

  if (reason == "authorization_pending") {
    outcome.state = auth_state::pending;
    return outcome;
  }
  if (reason == "slow_down") {
    // Adopt the server's interval -- it is the server's to set, and ignoring it
    // is how a client gets stuck in slow_down forever. The contract returns the
    // new value; fall back to +5s if it ever does not.
    code.interval = parsed.value("interval", code.interval + 5);
    outcome.state = auth_state::slow_down;
    return outcome;
  }
  if (reason == "access_denied") {
    outcome.state = auth_state::denied;
    outcome.error = "the sign-in was denied";
    return outcome;
  }
  if (reason == "expired_token") {
    outcome.state = auth_state::expired;
    outcome.error = "the sign-in code expired";
    return outcome;
  }

  outcome.error = reason.empty()
      ? std::string("the backplane returned HTTP ") + std::to_string(status)
      : reason;
  return outcome;
}

auth_outcome device_auth::wait_for_approval(device_code& code,
                                            const std::function<bool()>& cancel)
{
  auth_outcome outcome;
  while (true) {
    if (cancel && cancel()) {
      outcome.state = auth_state::error;
      outcome.error = "cancelled";
      return outcome;
    }

    outcome = poll(code);
    if (!outcome.polling()) {
      return outcome;
    }

    // poll() has already raised code.interval if the server asked us to slow
    // down, so this wait is correct for both states.
    m_sleep(code.interval);
  }
}

}  // namespace backplane
