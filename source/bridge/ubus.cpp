// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <string>
#include <utility>

#include "bridge/ubus.h"

namespace bridge
{
namespace
{
// ubus status codes worth naming. Anything else is reported numerically rather
// than guessed at.
const char* describe_ubus_status(int status)
{
  switch (status) {
    case 2:
      return "invalid argument";
    case 3:
      return "method not found";
    case 4:
      return "not found";
    case 6:
      return "permission denied";
    case 7:
      return "timeout";
    case 8:
      return "not supported";
    default:
      return nullptr;
  }
}
}  // namespace

std::string build_request(const std::string& method, const nlohmann::json& args)
{
  // ubus takes exactly four params: session, object, method, arguments. The
  // arguments must be an object even when empty.
  nlohmann::json request {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "call"},
      {"params",
       nlohmann::json::array(
           {std::string(k_no_session),
            "rist2rist",
            method,
            args.is_object() ? args : nlohmann::json::object()})},
  };
  return request.dump();
}

ubus_result parse_response(int http_status, const std::string& body)
{
  ubus_result result;
  result.http_status = http_status;

  if (http_status == 0) {
    result.error_code = "no_response";
    result.error = "no response from the bridge";
    return result;
  }

  // uhttpd answers non-200 itself when the endpoint or the ACL is wrong, so the
  // body is then not JSON-RPC at all.
  if (http_status != 200) {
    result.error_code = "http_" + std::to_string(http_status);
    result.error = "the bridge returned HTTP " + std::to_string(http_status);
    return result;
  }

  nlohmann::json reply;
  try {
    reply = nlohmann::json::parse(body);
  } catch (const nlohmann::json::exception&) {
    result.error_code = "malformed_reply";
    result.error = "the bridge did not return JSON-RPC";
    return result;
  }

  // A JSON-RPC error object is not an unexpected shape: it is the bridge SAYING
  // something, and its message is the most useful line in the whole reply.
  // Called "unexpected", it was discarded -- and this is exactly where the
  // control endpoint's "unknown method" was lost, leaving a panel that reported
  // a nameless failure while the bridge had named it precisely.
  if (reply.contains("error") && reply["error"].is_object()) {
    const auto& e = reply["error"];
    result.error_code = "jsonrpc_" + std::to_string(e.value("code", 0));
    result.error =
        e.value("message", std::string("the bridge refused the call"));
    return result;
  }

  if (!reply.contains("result") || !reply["result"].is_array()
      || reply["result"].size() < 2 || !reply["result"][1].is_object())
  {
    result.error_code = "unexpected_reply";
    result.error = "unexpected JSON-RPC reply from the bridge";
    return result;
  }

  result.ubus_status = reply["result"][0].is_number_integer()
      ? reply["result"][0].get<int>()
      : -1;
  result.data = reply["result"][1];

  if (result.ubus_status != 0) {
    // ubus or the ACL refused it before our plugin ran, so there is no plugin
    // payload to read.
    result.error_code = "ubus_" + std::to_string(result.ubus_status);
    const char* described = describe_ubus_status(result.ubus_status);
    result.error = described != nullptr
        ? described
        : "ubus refused the call (" + std::to_string(result.ubus_status) + ")";
    return result;
  }

  result.ok = result.data.value("ok", false);
  if (!result.ok) {
    result.error_code = result.data.value("error", std::string("failed"));
    result.error = result.data.value(
        "message", std::string("the bridge refused the call"));
  }
  return result;
}

ubus_client::ubus_client(std::string base_url,
                         std::string pair_token,
                         transport_fn transport)
    : m_base_url(std::move(base_url))
    , m_token(std::move(pair_token))
    , m_transport(std::move(transport))
{
}

ubus_result ubus_client::call(const std::string& method,
                              const nlohmann::json& args)
{
  if (!m_transport) {
    ubus_result result;
    result.error_code = "no_transport";
    result.error = "no transport is configured for the bridge";
    return result;
  }

  nlohmann::json merged = args.is_object() ? args : nlohmann::json::object();
  // The token rides in the arguments, not in a header: our ACL grants the
  // methods to the unauthenticated group and the plugin checks the token
  // itself.
  if (!m_token.empty()) {
    merged["token"] = m_token;
  }

  const auto [status, body] =
      m_transport(m_base_url + "/ubus", build_request(method, merged));
  return parse_response(status, body);
}

ubus_result ubus_client::claim(const std::string& device_uid)
{
  return call("claim", nlohmann::json {{"device_uid", device_uid}});
}

ubus_result ubus_client::get_config()
{
  return call("get_config", nlohmann::json::object());
}

ubus_result ubus_client::reconcile(const nlohmann::json& desired)
{
  return call("reconcile", desired);
}

ubus_result ubus_client::set_link_secret(const std::string& psk, int aes)
{
  nlohmann::json args;
  args["link_secret"] = psk;
  args["link_secret_aes"] = aes;
  return call("set_link_secret", args);
}

ubus_result ubus_client::release()
{
  return call("release", nlohmann::json::object());
}

ubus_result ubus_client::reload()
{
  return call("reload", nlohmann::json::object());
}

std::string claim_token(const ubus_result& result)
{
  if (!result.ok) {
    return {};
  }
  return result.data.value("token", std::string {});
}

}  // namespace bridge
