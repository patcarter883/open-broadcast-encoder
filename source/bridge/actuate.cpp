// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <string>
#include <utility>

#include "bridge/actuate.h"

#include "bridge/join_interface.h"
#include "lib/lib.h"

using json = nlohmann::json;

namespace bridge
{

namespace
{
// RIST carries a per-leg key in the URL, so the secret can apply to the WAN
// output without touching the bridge's clear listen leg. libRIST reads
// `secret=` and `aes-type=` from the query string.
std::string with_leg_secret(std::string url, const std::string& secret, int aes)
{
  if (secret.empty()) {
    return url;
  }
  const char sep = url.find('?') == std::string::npos ? '?' : '&';
  url += sep;
  url += "secret=";
  url += secret;
  if (aes == 128 || aes == 256) {
    url += "&aes-type=";
    url += std::to_string(aes);
  }
  return url;
}
}  // namespace

json bridge_desired_config(const actuate_request& req,
                           const hosted_session& session)
{
  json desired;
  if (!req.listen_url.empty()) {
    // A multicast listener must name the interface it joins the group on, or
    // libRIST binds the address without joining and receives nothing. rpcd's
    // whitelist takes `listen_url` as one string and nothing beside it, so the
    // join travels as `?miface=` inside that URL.
    desired["listen_url"] = with_multicast_interface(
        req.listen_url,
        resolve_join_interface(
            req.listen_url, req.listen_miface, req.interface_name));
  }

  json outputs = json::array();
  if (!session.rist_url.empty()) {
    json out;
    // Encryption is opt-in and applies only to this output leg; the listen
    // leg above stays as configured (clear).
    out["address"] = req.encrypt_wan_leg
        ? with_leg_secret(session.rist_url, session.psk, session.psk_aes)
        : session.rist_url;
    outputs.push_back(std::move(out));
  }
  desired["outputs"] = std::move(outputs);
  return desired;
}

namespace
{
// A bridge binds its media port to a wildcard so it accepts RIST on every
// interface. That is a BIND address, not a destination: sending to it loops
// back to this host. Keep the bridge's port, use the host we reached it on.
//
// Any `miface=` in the reported URL is dropped: it names a device on the
// BRIDGE, and this encoder sending with it would try to bind an interface this
// host does not have.
std::string sendable_target(const std::string& listen_url,
                            const std::string& host_hint)
{
  const std::string url = without_multicast_interface(listen_url);
  if (url.rfind("rist://", 0) != 0) {
    return url;
  }
  const auto colon = url.rfind(':');
  if (colon == std::string::npos || colon <= 6) {
    return url;
  }
  const std::string host = url.substr(7, colon - 7);
  const bool wildcard =
      host.empty() || host == "0.0.0.0" || host == "::" || host == "[::]";
  if (!wildcard) {
    return url;
  }
  std::string hint = host_hint;
  const auto hint_colon = hint.rfind(':');
  if (hint_colon != std::string::npos) {
    hint = hint.substr(0, hint_colon);  // the hint carries an API port
  }
  if (hint.empty()) {
    return url;
  }
  return "rist://" + hint + url.substr(colon);
}
}  // namespace

actuator::actuator(allocate_fn allocate,
                   reconcile_fn reconcile,
                   report_fn report,
                   credential_fn credential)
    : m_allocate {std::move(allocate)}
    , m_reconcile {std::move(reconcile)}
    , m_report {std::move(report)}
    , m_credential {std::move(credential)}
{
}

actuate_outcome actuator::run(const actuate_request& req,
                              std::chrono::milliseconds window)
{
  actuate_outcome out;

  // 1. The session first: everything below depends on where the allocator put
  // us.
  const alloc_result alloc = m_allocate(req.pop, req.encoder_name);
  if (!alloc.ok) {
    out.error_code = "allocate_failed";
    out.error = alloc.error;
    return out;
  }
  out.session = alloc.session;

  // Encryption is opt-in per encoder. Asking for it without a key would leave
  // the WAN leg in the clear while the UI says otherwise, so refuse instead.
  if (req.encrypt_wan_leg && alloc.session.psk.empty()) {
    out.error_code = "no_psk";
    out.error = "WAN encryption is enabled but the allocation carried no key";
    return out;
  }
  if (req.encrypt_wan_leg) {
    secrets::register_secret(alloc.session.psk);
  }

  // 2. Which bridge? The PORTAL's answer is authoritative (DT-20.1): the
  // allocation
  //    states the chain the operator configured. The request's bridge_uid is
  //    only a fallback, for the self-host and manual paths where nothing states
  //    it.
  const std::string bridge_uid = !alloc.session.bridge_uid.empty()
      ? alloc.session.bridge_uid
      : req.bridge_uid;

  // Direct. Nothing to apply, nothing to report, and this is a SUCCESS -- "no
  // bridge" is a decision the operator made, not a step that went missing.
  if (bridge_uid.empty()) {
    out.ok = true;
    out.encoder_target = alloc.session.rist_url;
    return out;
  }
  out.bridge_uid = bridge_uid;

  // 3. The token. A bridge this encoder claimed itself has one locally. One the
  //    PORTAL holds may not be here at all: the bridge keeps only a hash, so
  //    the portal's copy is the only one left. An empty result is NOT a failure
  //    -- it is what a virgin bridge returns (409 no_token), and claiming is
  //    how a token comes to exist. decide() is what refuses a claimed bridge we
  //    hold no token for, with a message that says so; second-guessing it here
  //    would break path C.
  std::string token = req.known_token;
  if (token.empty() && m_credential && alloc.session.bridge_id != 0) {
    std::string err;
    token = m_credential(alloc.session.bridge_id, err);
  }

  // 4. The bridge. Its upstream target is the node the allocator just picked,
  // which
  //    is exactly why this cannot be configured before the allocation.
  reconcile_request rr;
  rr.bridge_uid = bridge_uid;
  rr.fingerprint = req.fingerprint;
  rr.known_token = token;
  rr.encoder_uid = req.encoder_uid;
  rr.allow_claim = req.allow_claim;
  rr.desired = bridge_desired_config(req, alloc.session);

  const reconcile_outcome rec = m_reconcile(rr, window);
  out.action = rec.action;
  out.new_token = rec.new_token;
  out.report = rec.report;

  if (!rec.ok) {
    // The session IS live. Return it rather than a bare failure, so the caller
    // can see what exists and release it -- a half-applied chain must not be
    // hidden behind a generic error.
    out.error_code = rec.error_code.empty() ? "bridge_failed" : rec.error_code;
    out.error = rec.error;
    return out;
  }
  out.bridged = true;

  // A multicast listen leg that carries no join interface binds the group
  // address without joining it, so the bridge receives nothing while its own
  // state says it is listening. Nothing here can name a device on that router,
  // so it is raised rather than guessed at.
  out.warning = join_warning(rr.desired.value("listen_url", std::string {}));

  // Send to the BRIDGE, not to the node. Take the listen URL from what the
  // bridge reported back rather than from what we asked for: if it refused or
  // moved it, the reported value is the one that carries media.
  //
  // That URL is the bridge's BIND address, and a bridge binds a wildcard so it
  // accepts media on any interface. A wildcard is not a destination -- sending
  // to it loops straight back to this host -- so the host is replaced with the
  // address the bridge was actually reached on.
  const json& reported = rec.report.reported_config;
  const std::string reported_listen = reported.is_object()
      ? reported.value("listen_url", req.listen_url)
      : req.listen_url;
  const std::string host_hint = !rec.service.address.empty()
      ? rec.service.address
      : alloc.session.bridge_lan_host;
  out.encoder_target = sendable_target(reported_listen, host_hint);
  if (out.encoder_target.empty()) {
    // Bridged with no listen address: there is nowhere to send. Fail loudly
    // instead of falling back to the node's URL, which would bypass the bridge
    // silently.
    out.bridged = false;
    out.error_code = "no_listen_url";
    out.error = "the bridge reported no listen_url";
    return out;
  }

  // 5. Report. This is the one moment the portal can learn the token -- the
  // bridge
  //    stores only a hash, so a claim's response is the only copy.
  const report_result rep = m_report(rec.service, rec.report, rec.new_token);
  out.bridge_id = rep.bridge_id != 0 ? rep.bridge_id : alloc.session.bridge_id;
  out.reported = rep.ok;
  if (!rep.ok) {
    // The LAN is configured and carrying; only the portal's mirror is stale.
    // That is a real problem, but not one that makes the transport unusable --
    // so it is reported on the outcome rather than failing the run.
    out.error_code = "report_failed";
    out.error = rep.error;
  }

  out.ok = true;
  return out;
}

}  // namespace bridge
