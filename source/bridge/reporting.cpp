// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "bridge/reporting.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "lib/lib.h"

namespace bridge
{
namespace
{
constexpr const char* k_bridge_path = "/api/v1/bridges";

// The backplane validates api_version as an integer in 1..65535, while the
// advertisement carries it as text.
constexpr int k_api_min = 1;
constexpr int k_api_max = 65535;
}  // namespace

int api_version_number(const mdns::service& service)
{
  const std::string text = service.txt_value(k_txt_api);
  if (text.empty()) {
    return k_api_min;
  }
  try {
    const int value = std::stoi(text);
    if (value >= k_api_min && value <= k_api_max) {
      return value;
    }
  } catch (const std::exception&) {
    // Fall through: an unparseable version is reported as the oldest known one
    // rather than failing the report entirely.
  }
  return k_api_min;
}

nlohmann::json bridge_report_body(const mdns::service& service,
                                  const bridge_report& report,
                                  const std::string& new_token)
{
  nlohmann::json state = report.reported_config.is_object()
                             ? report.reported_config
                             : nlohmann::json::object();
  // `managed` is mirrored from HERE by the backplane. Nesting it in the reported
  // config is what makes the column honest: it is what the bridge said about itself.
  state["managed"] = report.managed;

  nlohmann::json body{
      {"bridge_uid", service.instance},
      {"api_version", api_version_number(service)},
      {"reported_config", state},
  };

  if (!report.address.empty()) {
    body["lan_host"] = report.address;
  }

  nlohmann::json health{{"ok", report.last_error.empty()}};
  if (!report.last_error.empty()) {
    health["last_error"] = report.last_error;
  }
  body["health"] = health;

  // Sent ONLY when this run claimed the bridge. Every other report leaves the token
  // alone so a routine health update cannot overwrite or blank it.
  if (!new_token.empty()) {
    body["pair_token"] = new_token;
  }

  return body;
}

bridge_reporter::bridge_reporter(std::string base_url,
                                 std::string device_token,
                                 backplane_transport_fn transport)
    : m_base(std::move(base_url)),
      m_device_token(std::move(device_token)),
      m_transport(std::move(transport))
{
}

report_result bridge_reporter::report(const mdns::service& service,
                                      const bridge_report& report,
                                      const std::string& new_token)
{
  report_result result;

  if (!m_transport) {
    result.error = "no transport is configured";
    return result;
  }

  const std::string body = bridge_report_body(service, report, new_token).dump();
  const auto [status, response] =
      m_transport("POST", k_bridge_path, m_device_token, body);
  result.http_status = status;

  if (status == 0) {
    result.error = "no response from the backplane";
    return result;
  }

  const bool http_ok = status == 200 || status == 201;

  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(response);
  } catch (const nlohmann::json::exception&) {
    // Not JSON, so the status is all we have to go on.
    result.error = http_ok
                       ? std::string("the backplane did not return JSON")
                       : std::string("the backplane returned HTTP ") +
                             std::to_string(status);
    return result;
  }

  if (!http_ok) {
    // The backplane explains rejections in `message` (a validation failure names the
    // field). Preferring that over the bare status is the difference between an
    // operator fixing their input and guessing.
    result.error = parsed.value("message",
                                std::string("the backplane returned HTTP ") +
                                    std::to_string(status));
    return result;
  }

  if (!parsed.value("ok", false)) {
    result.error = parsed.value(
        "message", std::string("the backplane refused the report"));
    return result;
  }

  const auto bridge = parsed.value("bridge", nlohmann::json::object());
  result.bridge_id =
      bridge.is_object() ? bridge.value("id", static_cast<long>(0)) : 0;
  result.ok = true;
  return result;
}

std::string bridge_reporter::credential(long bridge_id, std::string& error)
{
  error.clear();

  if (!m_transport) {
    error = "no transport is configured";
    return {};
  }

  const auto [status, response] = m_transport(
      "POST",
      std::string(k_bridge_path) + "/" + std::to_string(bridge_id) + "/credential",
      m_device_token, "{}");

  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(response);
  } catch (const nlohmann::json::exception&) {
    error = status == 0 ? "no response from the backplane"
                        : "the backplane did not return JSON";
    return {};
  }

  if (status == 409) {
    // No token held. This is the ordinary answer for a virgin bridge, and it means
    // "claim it", not "something is broken".
    error = parsed.value("error_code", std::string("no_token"));
    return {};
  }
  if (status != 200 || !parsed.value("ok", false)) {
    if (status == 0) {
      error = "no response from the backplane";
    } else {
      error = parsed.value("message",
                           std::string("the backplane returned HTTP ") +
                               std::to_string(status));
    }
    return {};
  }

  const auto credential = parsed.value("credential", nlohmann::json::object());
  if (!credential.is_object()) {
    error = "the backplane returned no credential";
    return {};
  }

  const std::string token = credential.value("token", std::string{});
  if (!token.empty()) {
    // A credential on the wire: register it so it cannot reach a log pane (H2).
    secrets::register_secret(token);
  }
  return token;
}

}  // namespace bridge
