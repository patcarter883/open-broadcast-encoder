// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// ONE Allocate action: allocate the hosted session, then -- if the portal put a
// bridge in the chain -- apply that bridge's config over the LAN and report it
// back (DT-20.1, DT-21, DT-22).
//
// Why one action and not three buttons: the bridge's desired state depends on the
// ALLOCATION. The bridge must forward to whichever node the allocator picked, and
// that is not known until the session exists. Configure the bridge first and it
// points somewhere the session is not; split the steps and there is a window in
// which a live session exists and nothing is on the air.
//
// The encoder is the actuator -- the one machine that can see both the backplane
// and the customer LAN (BACKPLANE.md §49: the bridge never contacts the backplane).
// The portal records what SHOULD be; this applies it.
//
// All three collaborators are injected, so the ordering, the target the encoder
// ends up with, and the failure handling are unit-testable without a backplane or
// a bridge.

#pragma once

#include <chrono>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "backplane/backplane.h"
#include "bridge/bridge_client.h"
#include "bridge/mdns.h"
#include "bridge/reporting.h"

namespace bridge
{

// Everything the caller knows when it asks for a session. The bridge_* fields
// normally come from the allocation response; the token/fingerprint are local state
// the encoder holds from an earlier claim.
struct actuate_request
{
  std::string pop;             // placement preference; empty = the account's default
  std::string bridge_uid;      // the portal's chosen bridge; empty = direct
  std::string bridge_address;  // last reported LAN address; empty = browse for it
  std::string listen_url;      // what the bridge should listen on
  std::string interface_name;  // the bridge's egress interface; may be empty
  std::string fingerprint;     // recorded at fulfilment; empty when not known
  std::string known_token;     // the pair token we already hold, if any
  std::string encoder_uid;     // who is claiming (self-asserted, never trusted)
  bool allow_claim = true;
};

struct actuate_outcome
{
  // ok: the CHAIN is live -- a session exists and, if the portal asked for a
  // bridge, that bridge is configured. reported: the portal's mirror is up to date.
  // They are separate because a bridge can be carrying media while the backplane
  // has not yet heard about it; collapsing them would call a working transport
  // broken, or a missing report harmless.
  bool ok = false;
  bool reported = false;
  bool bridged = false;

  hosted_session session;      // set whenever an allocation succeeded
  // Where the ENCODER should send. The bridge's listen_url when bridged (read back
  // from the bridge, not assumed), else the node's rist_url. If the encoder kept
  // dialling rist_url while bridged, the bridge would sit in the chain un-used.
  std::string encoder_target;

  bridge_action action = bridge_action::none;
  std::string new_token;       // set only when THIS run claimed the bridge
  bridge_report report;
  long bridge_id = 0;          // the backplane's row id, for a credential fetch
  std::string error_code;      // empty on full success
  std::string error;
};

class actuator
{
public:
  using allocate_fn = std::function<alloc_result(const std::string& pop)>;
  using reconcile_fn = std::function<reconcile_outcome(
      const reconcile_request&, std::chrono::milliseconds)>;
  using report_fn = std::function<report_result(
      const mdns::service&, const bridge_report&, const std::string& new_token)>;

  actuator(allocate_fn allocate, reconcile_fn reconcile, report_fn report);

  // BLOCKING -- browse, ubus and HTTP. Run it off the UI thread.
  actuate_outcome run(const actuate_request& req, std::chrono::milliseconds window);

private:
  allocate_fn m_allocate;
  reconcile_fn m_reconcile;
  report_fn m_report;
};

// The bridge's desired state, derived from the session just allocated. Pure, so
// the mapping DT-20 exists to make single-sourced is asserted rather than assumed:
// the bridge forwards to the node's ingest URL.
nlohmann::json bridge_desired_config(const actuate_request& req,
                                     const hosted_session& session);

}  // namespace bridge
