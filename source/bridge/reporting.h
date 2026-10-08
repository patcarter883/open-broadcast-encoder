// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// Reporting a bridge to the backplane, and fetching the credential the portal
// already holds (DT-19, DT-21).
//
// The encoder is the only thing here that holds a credential AND the only thing
// that can see the customer LAN, so it does both jobs: it reports what the
// bridge IS, and the portal records what the bridge SHOULD BE. The backplane
// never contacts a bridge and a bridge never contacts the backplane
// (BACKPLANE.md §49).
//
// The HTTP call is injected, matching backplane_client's seam, so the wire
// contract is unit-testable against a live-less backplane.

#pragma once

#include <functional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "bridge/bridge_client.h"
#include "bridge/mdns.h"

namespace bridge
{

// The POST /v1/bridges body. Pure, so the shape is asserted rather than
// assumed.
//
// Two things about this contract are easy to get wrong:
//  - The backplane mirrors `managed` from reported_config.managed. There is
//    deliberately NO top-level `managed`, because the bridge is authoritative
//    about it and a caller must not be able to assert it.
//  - `api_version` must be an INTEGER there, while the advertisement carries it
//  as
//    TXT text.
nlohmann::json bridge_report_body(const mdns::service& service,
                                  const bridge_report& report,
                                  const std::string& new_token);

// The advertisement's api version as the integer the backplane validates,
// defaulting to 1 when absent or unusable.
int api_version_number(const mdns::service& service);

struct report_result
{
  bool ok = false;
  long bridge_id = 0;  // the backplane's row id, needed for the credential call
  std::string error;
  int http_status = 0;
};

// (method, path, bearer_token, body) -> (http_status, body). Status 0 means the
// request never reached the server.
using backplane_transport_fn =
    std::function<std::pair<int, std::string>(const std::string& method,
                                              const std::string& path,
                                              const std::string& token,
                                              const std::string& body)>;

class bridge_reporter
{
public:
  bridge_reporter(std::string base_url,
                  std::string device_token,
                  backplane_transport_fn transport = {});

  // Report the bridge's presence, state and health. Idempotent on the
  // backplane: keyed on (account, bridge_uid). `new_token` is set only when
  // THIS run claimed the bridge -- that is the one moment the portal can learn
  // the token, because the bridge itself stores only a hash.
  report_result report(const mdns::service& service,
                       const bridge_report& report,
                       const std::string& new_token);

  // The pair token the portal holds, so an encoder that did NOT do the claiming
  // can still drive the bridge. Empty with `error` set when none is held (409)
  // -- which is normal for a virgin bridge and means "claim it first".
  std::string credential(long bridge_id, std::string& error);

private:
  std::string m_base;
  std::string m_device_token;
  backplane_transport_fn m_transport;
};

}  // namespace bridge

// Build an httplib-backed transport for the reporter's endpoints. Defined in
// reporting_httplib.cpp so this header stays free of httplib and links into the
// tests.
bridge::backplane_transport_fn make_bridge_reporter_transport(
    const std::string& base_url, const std::string& device_token);
