// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <utility>

#include "bridge/actuate.h"

using json = nlohmann::json;

namespace bridge
{

json bridge_desired_config(const actuate_request& req,
                           const hosted_session& session)
{
  json desired;
  if (!req.listen_url.empty()) {
    desired["listen_url"] = req.listen_url;
  }

  json outputs = json::array();
  if (!session.rist_url.empty()) {
    json out;
    out["address"] = session.rist_url;
    out["weight"] = "1";
    if (!req.interface_name.empty()) {
      out["interface"] = req.interface_name;
    }
    outputs.push_back(std::move(out));
  }
  desired["outputs"] = std::move(outputs);
  return desired;
}

actuator::actuator(allocate_fn allocate, reconcile_fn reconcile, report_fn report)
    : m_allocate {std::move(allocate)}
    , m_reconcile {std::move(reconcile)}
    , m_report {std::move(report)}
{
}

actuate_outcome actuator::run(const actuate_request& req,
                              std::chrono::milliseconds window)
{
  actuate_outcome out;

  // 1. The session first: everything below depends on where the allocator put us.
  const alloc_result alloc = m_allocate(req.pop);
  if (!alloc.ok) {
    out.error_code = "allocate_failed";
    out.error = alloc.error;
    return out;
  }
  out.session = alloc.session;

  // 2. Direct. The portal chose no bridge, so the encoder dials the node itself.
  //    Nothing to apply and nothing to report, and this is a SUCCESS -- "no bridge"
  //    is a decision the operator made, not a step that went missing.
  if (req.bridge_uid.empty()) {
    out.ok = true;
    out.encoder_target = alloc.session.rist_url;
    return out;
  }

  // 3. The bridge. Its upstream target is the node the allocator just picked, which
  //    is exactly why this cannot be configured before the allocation.
  reconcile_request rr;
  rr.bridge_uid = req.bridge_uid;
  rr.fingerprint = req.fingerprint;
  rr.known_token = req.known_token;
  rr.encoder_uid = req.encoder_uid;
  rr.allow_claim = req.allow_claim;
  rr.desired = bridge_desired_config(req, alloc.session);

  const reconcile_outcome rec = m_reconcile(rr, window);
  out.action = rec.action;
  out.new_token = rec.new_token;
  out.report = rec.report;

  if (!rec.ok) {
    // The session IS live. Return it rather than a bare failure, so the caller can
    // see what exists and release it -- a half-applied chain must not be hidden
    // behind a generic error.
    out.error_code = rec.error_code.empty() ? "bridge_failed" : rec.error_code;
    out.error = rec.error;
    return out;
  }
  out.bridged = true;

  // Send to the BRIDGE, not to the node. Take the listen URL from what the bridge
  // reported back rather than from what we asked for: if it refused or moved it,
  // the reported value is the one that carries media.
  const json& reported = rec.report.reported_config;
  out.encoder_target = reported.is_object()
                           ? reported.value("listen_url", req.listen_url)
                           : req.listen_url;
  if (out.encoder_target.empty()) {
    // Bridged with no listen address: there is nowhere to send. Fail loudly instead
    // of falling back to the node's URL, which would bypass the bridge silently.
    out.bridged = false;
    out.error_code = "no_listen_url";
    out.error = "the bridge reported no listen_url";
    return out;
  }

  // 4. Report. This is the one moment the portal can learn the token -- the bridge
  //    stores only a hash, so a claim's response is the only copy.
  const report_result rep = m_report(rec.service, rec.report, rec.new_token);
  out.bridge_id = rep.bridge_id;
  out.reported = rep.ok;
  if (!rep.ok) {
    // The LAN is configured and carrying; only the portal's mirror is stale. That
    // is a real problem, but not one that makes the transport unusable -- so it is
    // reported on the outcome rather than failing the run.
    out.error_code = "report_failed";
    out.error = rep.error;
  }

  out.ok = true;
  return out;
}

}  // namespace bridge
