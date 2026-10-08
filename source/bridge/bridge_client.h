// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The bridge orchestrator: find the bridge the portal means, work out what to
// do with it, do it, and report back (DT-19, DT-20, DT-21).
//
// The portal records desired state; the ENCODER applies it on the LAN. The
// bridge never contacts the backplane and holds no fleet credential.
//
// The two decisions that matter -- which bridge, and whether it may be claimed
// -- are PURE functions, because they are the security-relevant ones.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "bridge/mdns.h"
#include "bridge/ubus.h"

namespace bridge
{

// TXT keys an advertisement may carry. Only a fingerprint is ever published --
// never the token (DT-21, H2).
inline constexpr const char* k_txt_fingerprint = "fingerprint";
inline constexpr const char* k_txt_api = "api";
inline constexpr const char* k_txt_claimed = "claimed";
inline constexpr const char* k_txt_managed = "managed";
inline constexpr const char* k_txt_api_port = "api_port";

// What the portal asks for.
struct reconcile_request
{
  std::string bridge_uid;  // matches the advertised mDNS instance
  std::string fingerprint;  // recorded at fulfilment; empty when not known
  std::string known_token;  // the pair token the portal holds, if any
  nlohmann::json desired;  // listen_url + outputs, the portal's desired state
  bool allow_claim =
      true;  // false where claiming a virgin bridge is not wanted
  // Who is claiming, for the bridge to record. A claim is UNAUTHENTICATED --
  // the token comes FROM the bridge -- so this is a self-asserted hint about
  // which controller took the bridge, never a basis for trust.
  std::string encoder_uid;
};

// Health and state as reported back to the backplane.
struct bridge_report
{
  std::string bridge_uid;
  std::string address;
  std::string api_version;
  bool managed = false;
  nlohmann::json reported_config;
  std::string last_error;
};

enum class bridge_action
{
  none,  // nothing to do, or refused
  claim,  // mint a token: only ever valid on a virgin bridge
  apply,  // reconcile using the token we already hold
};

struct bridge_decision
{
  bridge_action action = bridge_action::none;
  bool ok = false;  // whether to proceed at all
  std::string token;
  std::string error_code;
  std::string error;
};

struct reconcile_outcome
{
  bool ok = false;
  bridge_action action = bridge_action::none;
  std::string new_token;  // set only when THIS call claimed the bridge
  bridge_report report;
  // The advertisement this outcome came from. The report body needs it -- the
  // address and the API version live in the TXT record -- and rebuilding that
  // from the report alone would lose them, while re-browsing would be a second
  // browse.
  mdns::service service;
  std::string error_code;
  std::string error;
};

// The ubus endpoint for a discovered bridge. rpcd is reached over uhttpd, so
// this is the WEB port -- not the RIST listen port in the SRV record. Pure.
std::string ubus_base_url(const mdns::service& service);

struct find_result
{
  bool found = false;
  mdns::service service;
  std::string error_code;
  std::string error;
};

// Pick the bridge the request names out of what the browse found. Pure. With no
// bridge_uid, a lone bridge on the LAN is taken -- which is what makes LAN
// auto-claim work; two bridges and no uid is ambiguous, not a guess.
find_result find_bridge(const std::vector<mdns::service>& found,
                        const reconcile_request& request);

// Decide what to do from the bridge's OWN published state. Pure. This is where
// the DT-21 states are enforced: virgin, claimed-by-us, and
// reset-since-fulfilment.
bridge_decision decide(const mdns::service& service,
                       const reconcile_request& request);

class bridge_client
{
public:
  using discover_fn =
      std::function<std::vector<mdns::service>(std::chrono::milliseconds)>;
  using ubus_factory_fn = std::function<std::unique_ptr<ubus_client>(
      const std::string& base_url, const std::string& token)>;

  bridge_client(discover_fn discover, ubus_factory_fn make_ubus);

  // Browse, decide, apply, read back. BLOCKING: run it off the UI thread.
  reconcile_outcome reconcile(const reconcile_request& request,
                              std::chrono::milliseconds window);

private:
  discover_fn m_discover;
  ubus_factory_fn m_make_ubus;
};

}  // namespace bridge
