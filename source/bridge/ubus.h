// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The rist2rist bridge control surface, over ubus JSON-RPC (DT-19, DT-21).
//
// The encoder is the actuator: the portal records desired state in the
// backplane, the encoder fetches it and applies it to the bridge over the LAN.
// The bridge never contacts the backplane and holds no fleet credential
// (BACKPLANE.md §49).
//
// This header is the PURE half -- envelope construction and reply parsing -- so
// the protocol contract is unit-testable without a router. ubus_httplib.cpp
// supplies the socket.

#pragma once

#include <functional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace bridge
{

// The session id ubus uses for a call made WITHOUT a login. Our ACL exposes the
// rist2rist methods through a group literally named "unauthenticated" and the
// plugin authenticates on the pair token instead, so no session is ever needed
// -- which is what lets the encoder configure a bridge without touching the
// router.
inline constexpr const char* k_no_session = "00000000000000000000000000000000";

struct ubus_result
{
  bool ok = false;  // the plugin's own `ok`
  int http_status = 0;  // 0 = no HTTP response at all (transport failure)
  int ubus_status = 0;  // JSON-RPC result[0]: 0 = OK, 6 = denied, ...
  std::string error_code;  // the plugin's `error`, or a synthetic code
  std::string error;  // human-readable
  nlohmann::json data;  // the plugin's payload
};

// Build the JSON-RPC envelope for a rist2rist method call.
std::string build_request(const std::string& method,
                          const nlohmann::json& args);

// Parse a /ubus reply. Pure, so every failure mode is testable.
ubus_result parse_response(int http_status, const std::string& body);

class ubus_client
{
public:
  // Mirrors backplane_client's seam: the socket lives behind this, so tests
  // script it and the class stays free of I/O.
  using transport_fn = std::function<std::pair<int, std::string>(
      const std::string& url, const std::string& body)>;

  // base_url is the bridge, e.g. http://192.168.8.1
  ubus_client(std::string base_url,
              std::string pair_token = {},
              transport_fn transport = {});

  void set_transport(transport_fn transport)
  {
    m_transport = std::move(transport);
  }
  void set_token(std::string token) { m_token = std::move(token); }
  const std::string& token() const { return m_token; }
  const std::string& base_url() const { return m_base_url; }

  // claim is the ONLY method that works without a token: claiming is what
  // establishes one. Every other method on a claimed bridge needs it.
  ubus_result claim(const std::string& device_uid);
  ubus_result get_config();
  ubus_result reconcile(const nlohmann::json& desired);

  // Install the session's RIST passphrase on the bridge for its OUTPUT legs.
  // Its own method rather than a config field: the bridge's config whitelist
  // refuses secret-bearing fields by design, so a secret can never appear in a
  // config report. Write-only -- nothing reads the value back.
  ubus_result set_link_secret(const std::string& psk, int aes);

  ubus_result release();
  ubus_result reload();

  // Any method, with the token merged into the arguments.
  ubus_result call(const std::string& method, const nlohmann::json& args);

private:
  std::string m_base_url;
  std::string m_token;
  transport_fn m_transport;
};

// The token a successful claim returned, or empty. The plugin returns the
// plaintext exactly once and stores only its hash, so this is the only chance
// to take it.
std::string claim_token(const ubus_result& result);

// Build an httplib-backed transport. Registers the token as a secret so it
// cannot reach a UI log pane (H2: URLs are loggable by definition, credentials
// are not).
ubus_client::transport_fn make_ubus_transport(const std::string& pair_token);

}  // namespace bridge
