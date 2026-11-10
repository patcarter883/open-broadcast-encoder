// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <utility>
#include <vector>

#include "backplane/backplane.h"

#include "lib/lib.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string hosted_session::to_json() const
{
  json j;
  j["session_id"] = session_id;
  j["rist_url"] = rist_url;
  j["control_url"] = control_url;
  j["control_token"] = control_token;
  j["psk"] = psk;
  j["psk_aes"] = psk_aes;
  if (!state.empty()) {
    j["state"] = state;
  }
  // start_body is stored as parsed JSON so the file stays readable.
  if (!start_body_json.empty()) {
    j["start_body"] = json::parse(start_body_json, nullptr, false);
  }
  // DT-20.1: the chain decision is part of the allocation, so it persists with
  // it.
  j["bridge_present"] = bridge_present;
  if (bridge_present) {
    j["bridge_id"] = bridge_id;
    j["bridge_uid"] = bridge_uid;
    j["bridge_lan_host"] = bridge_lan_host;
  }
  return j.dump(2);
}

namespace
{
// nlohmann's value() returns the default only when the key is ABSENT; a key
// present as JSON null throws a type_error. An allocation carries null for
// whatever does not apply to the session (no PSK, no bridge), so every read
// tolerates null explicitly.
std::string str_or(const json& j, const char* key, std::string fallback)
{
  if (!j.is_object()) {
    return fallback;
  }
  const auto it = j.find(key);
  if (it == j.end() || !it->is_string()) {
    return fallback;
  }
  return it->get<std::string>();
}

long long_or(const json& j, const char* key, long fallback)
{
  if (!j.is_object()) {
    return fallback;
  }
  const auto it = j.find(key);
  if (it == j.end() || !it->is_number_integer()) {
    return fallback;
  }
  return it->get<long>();
}

bool bool_or(const json& j, const char* key, bool fallback)
{
  if (!j.is_object()) {
    return fallback;
  }
  const auto it = j.find(key);
  if (it == j.end() || !it->is_boolean()) {
    return fallback;
  }
  return it->get<bool>();
}
}  // namespace

hosted_session hosted_session::from_json(const std::string& text)
{
  hosted_session s;
  const json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return s;
  }
  s.session_id = str_or(j, "session_id", "");
  s.rist_url = str_or(j, "rist_url", "");
  s.encoder_target = str_or(j, "encoder_target", "");
  s.control_url = str_or(j, "control_url", "");
  s.control_token = str_or(j, "control_token", "");
  s.psk = str_or(j, "psk", "");
  s.psk_aes = static_cast<int>(long_or(j, "psk_aes", 256));
  // Masked from the instant the record is read (H2).
  secrets::register_secret(s.psk);
  s.state = str_or(j, "state", "");
  if (j.contains("start_body")) {
    s.start_body_json = j.at("start_body").dump();
  }
  s.bridge_present = bool_or(j, "bridge_present", false);
  s.bridge_id = long_or(j, "bridge_id", 0);
  s.bridge_uid = str_or(j, "bridge_uid", "");
  s.bridge_lan_host = str_or(j, "bridge_lan_host", "");
  return s;
}

namespace
{
std::string state_of(const json& sess)
{
  // The portal's state word, defensively read: a non-string (an enum object, a
  // null) must not become the literal text "null" and then be classified.
  if (auto it = sess.find("state"); it != sess.end() && it->is_string()) {
    return it->get<std::string>();
  }
  return {};
}

hosted_session parse_session(const json& body)
{
  hosted_session s;
  if (!body.contains("session")) {
    return s;
  }
  const json& sess = body.at("session");
  s.session_id = str_or(sess, "id", "");
  s.rist_url = str_or(sess, "rist_url", "");
  s.encoder_target = str_or(sess, "encoder_target", "");
  s.control_url = str_or(sess, "control_url", "");
  s.control_token = str_or(sess, "control_token", "");
  s.psk = str_or(sess, "psk", "");
  s.psk_aes = static_cast<int>(long_or(sess, "psk_aes", 256));
  // Masked from the instant the allocation is read (H2).
  secrets::register_secret(s.psk);
  s.state = state_of(sess);
  if (sess.contains("start_body")) {
    s.start_body_json = sess.at("start_body").dump();
  }
  // DT-20.1: present-and-null means "direct"; ABSENT means this backplane
  // predates the field. Those must not collapse into one value -- see
  // hosted_session.
  if (sess.contains("bridge")) {
    s.bridge_present = true;
    const json& b = sess.at("bridge");
    if (b.is_object()) {
      s.bridge_id = b.value("id", 0L);
      s.bridge_uid = b.value("bridge_uid", "");
      s.bridge_lan_host = b.value("lan_host", "");
    }
  }
  return s;
}
}  // namespace

backplane_client::backplane_client(std::string base_url,
                                   std::string device_token,
                                   transport_fn transport)
    : m_base {std::move(base_url)}
    , m_device_token {std::move(device_token)}
    , m_transport {std::move(transport)}
{
}

std::string format_destination_list(
    const std::vector<backplane_client::destination_view>& destinations)
{
  if (destinations.empty()) {
    return "Hosted: this account has no saved destinations yet -- add one in "
           "the portal before allocating.\n";
  }
  std::string out =
      "Hosted destinations on this account (the portal decides the fan-out and "
      "its transcode):\n";
  for (const auto& d : destinations) {
    out += "  " + (d.label.empty() ? std::string {"(unlabelled)"} : d.label);
    out += " (" + (d.type.empty() ? std::string {"?"} : d.type) + ") -> ";
    if (d.transcode.empty()) {
      out += "copy";
    } else if (d.transcode == "h264") {
      out += "transcode H.264";
    } else if (d.transcode == "h265") {
      out += "transcode H.265";
    } else {
      // Anything the portal one day allows that this build does not word for:
      // still show it, because hiding a real setting is worse than an odd
      // label.
      out += "transcode " + d.transcode;
    }
    out += "\n";
  }
  return out;
}

backplane_client::destinations_result backplane_client::list_destinations()
{
  destinations_result r;
  if (!m_transport) {
    r.error = "no transport configured";
    return r;
  }
  // The real path is /api/v1/destinations. BACKPLANE.md writes every endpoint
  // as /v1/..., which 404s -- `artisan route:list` is the ground truth.
  const auto [status, body] =
      m_transport("GET", "/api/v1/destinations", m_device_token, "");
  if (status == 0) {
    r.error = "no response from backplane";
    return r;
  }
  const json resp = json::parse(body, nullptr, false);
  if (status / 100 != 2 || resp.is_discarded() || !resp.is_object()) {
    r.error = resp.is_object()
        ? resp.value("message", "could not read destinations")
        : "could not read destinations";
    return r;
  }
  for (const json& d : resp.value("destinations", json::array())) {
    if (!d.is_object()) {
      continue;
    }
    destination_view v;
    v.label = str_or(d, "label", "");
    v.type = str_or(d, "type", "");
    if (auto t = d.find("transcode"); t != d.end() && t->is_string()) {
      v.transcode = t->get<std::string>();
    }
    r.destinations.push_back(std::move(v));
  }
  r.ok = true;
  return r;
}

alloc_result backplane_client::allocate(const std::string& pop,
                                        const std::string& encoder_name,
                                        const bitrate_inputs& bitrates,
                                        const std::string& encoder_hostname)
{
  alloc_result r;
  if (!m_transport) {
    r.error = "no transport configured";
    return r;
  }
  // DT-22: the transport decision is never sent from here -- the fan-out and
  // recording come from the operator's transport row, and the backplane
  // refuses them (422) rather than silently preferring one. `encoder_name` is
  // identity, not transport: it lets one account read back which instance
  // allocated a session.
  //
  // The bitrate inputs steer the portal's transcode sizing and are sent one
  // field at a time: each is written only when a real figure exists, so the
  // absent case reaches the portal as absent and not as zero or as a guess.
  json req;
  req["schema_version"] = 1;
  if (!pop.empty()) {
    req["pop"] = pop;
  }
  if (!encoder_name.empty()) {
    req["encoder_name"] = encoder_name;
  }
  if (!encoder_hostname.empty()) {
    req["encoder_hostname"] = encoder_hostname;
  }
  if (bitrates.source_bitrate_kbps.has_value()) {
    req["source_bitrate_kbps"] = *bitrates.source_bitrate_kbps;
  }
  if (bitrates.measured_budget_kbps.has_value()) {
    req["measured_budget_kbps"] = *bitrates.measured_budget_kbps;
  }

  const auto [status, body] =
      m_transport("POST", "/api/v1/sessions", m_device_token, req.dump());
  r.http_status = status;
  if (status == 0) {
    r.error = "no response from backplane";
    return r;
  }
  const json resp = json::parse(body, nullptr, false);
  if (status / 100 != 2 || resp.is_discarded()) {
    r.error = resp.is_object() ? resp.value("message", "allocation failed")
                               : "allocation failed";
    return r;
  }
  r.session = parse_session(resp);
  if (!r.session.valid()) {
    r.error = "malformed allocation response";
    return r;
  }
  // M2.7: persist BEFORE handing the session back — the credentials are on
  // disk before the caller can use (and possibly crash on) them.
  if (m_persist) {
    m_persist(r.session);
  }
  r.ok = true;
  return r;
}

bool backplane_client::deallocate(const std::string& session_id,
                                  std::string& err)
{
  if (!m_transport) {
    err = "no transport configured";
    return false;
  }
  if (session_id.empty()) {
    return true;  // nothing to release
  }
  const auto [status, body] = m_transport(
      "DELETE", "/api/v1/sessions/" + session_id, m_device_token, "");
  if (status == 0) {
    err = "no response from backplane";
    return false;
  }
  // 404 = already gone (reaped): treat as success for the abandon flow.
  if (status / 100 == 2 || status == 404) {
    return true;
  }
  err = "deallocate failed (HTTP " + std::to_string(status) + ")";
  return false;
}

alloc_result backplane_client::abandon_and_reallocate(
    const std::string& lost_session_id,
    const std::string& pop,
    const std::string& encoder_name,
    const bitrate_inputs& bitrates,
    const std::string& encoder_hostname)
{
  std::string err;
  // Best-effort release of the orphan; a failure here (e.g. it was already
  // reaped) must not block getting a working session.
  deallocate(lost_session_id, err);
  return allocate(pop, encoder_name, bitrates, encoder_hostname);
}

namespace
{
// The portal's state words in lower case with surrounding space removed, so a
// "Running" and a "running " still classify. Classification must never depend
// on the portal's capitalisation.
std::string normalise_state(const std::string& state)
{
  std::string out;
  out.reserve(state.size());
  for (const char c : state) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      continue;
    }
    out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
  }
  return out;
}
}  // namespace

session_probe classify_probe(int http_status, bool body_usable)
{
  if (http_status == 0) {
    return session_probe::unavailable;  // never reached the portal
  }
  if (http_status == 401 || http_status == 403) {
    return session_probe::unauthorised;
  }
  if (http_status == 404) {
    return session_probe::gone;
  }
  if (http_status / 100 == 2) {
    // A 2xx we cannot read is not a session we can act on. Treating it as
    // "gone" would delete a live allocation, so it is retried instead.
    return body_usable ? session_probe::found : session_probe::unavailable;
  }
  return session_probe::unavailable;
}

reattach_action decide_reattach(session_probe probe, const std::string& state)
{
  switch (probe) {
    case session_probe::gone:
      // The portal does not have it: nothing exists to reattach to, and holding
      // the file would only make the next launch ask again.
      return reattach_action::clear_session;
    case session_probe::unavailable:
      return reattach_action::retry;
    case session_probe::unauthorised:
      // A refused device token is not a transient failure: retrying for five
      // minutes would just be five minutes of 401s. Keep the file -- the
      // session may well be live -- and tell the operator to sign in.
      return reattach_action::keep_report;
    case session_probe::found:
      break;
  }

  const std::string s = normalise_state(state);
  // Only a session that is meant to be up is reattached to. Re-posting /start
  // into anything else would resurrect a session that is being torn down.
  if (s == "allocated" || s == "running" || s == "live") {
    return reattach_action::reattach;
  }
  if (s == "ended" || s == "released" || s == "terminated" || s == "completed"
      || s == "expired" || s == "closed")
  {
    return reattach_action::clear_session;
  }
  // A document with no state word at all: the portal found the session we hold
  // and said nothing about it being over, so the session is reattached to.
  // Refusing here would cost the operator the destination for the whole of a
  // long session just because the response omitted a field.
  if (s.empty()) {
    return reattach_action::reattach;
  }
  // A state this build does not know is not worth guessing about: the session
  // may still be billable, so the file is kept and the operator told.
  return reattach_action::keep_report;
}

session_fetch_result backplane_client::get_session(
    const std::string& session_id)
{
  session_fetch_result r;
  if (!m_transport) {
    r.error = "no transport configured";
    return r;
  }
  if (session_id.empty()) {
    r.probe = session_probe::gone;
    r.error = "no session id to look up";
    return r;
  }

  const auto [status, body] =
      m_transport("GET", "/api/v1/sessions/" + session_id, m_device_token, "");
  r.http_status = status;
  const json resp = json::parse(body, nullptr, false);
  const bool has_session = resp.is_object() && resp.contains("session")
      && resp.at("session").is_object();
  r.probe = classify_probe(status, has_session);

  if (r.probe == session_probe::found) {
    // Only what the portal chose to restate. control_token is a show-once
    // secret, so a later read legitimately omits it: the caller merges this
    // over the persisted copy rather than treating an absent field as empty.
    r.session = parse_session(resp);
    r.state = r.session.state;
    r.ok = true;
    return r;
  }
  if (r.probe == session_probe::gone) {
    r.error = "session " + session_id + " is not on the backplane";
  } else if (r.probe == session_probe::unauthorised) {
    r.error = "backplane refused the device token (HTTP "
        + std::to_string(status) + ")";
  } else {
    r.error = status == 0 ? "no response from backplane"
                          : "could not read session " + session_id + " (HTTP "
            + std::to_string(status) + ")";
  }
  return r;
}

hosted_session merge_session(const hosted_session& persisted,
                             const hosted_session& fresh)
{
  hosted_session s = persisted;
  if (!fresh.session_id.empty()) {
    s.session_id = fresh.session_id;
  }
  if (!fresh.rist_url.empty()) {
    s.rist_url = fresh.rist_url;
  }
  if (!fresh.control_url.empty()) {
    s.control_url = fresh.control_url;
  }
  // Show-once secrets: only ever replaced by a value, never by silence.
  if (!fresh.control_token.empty()) {
    s.control_token = fresh.control_token;
  }
  if (!fresh.psk.empty()) {
    s.psk = fresh.psk;
  }
  if (fresh.psk_aes > 0) {
    s.psk_aes = fresh.psk_aes;
  }
  if (!fresh.start_body_json.empty()) {
    s.start_body_json = fresh.start_body_json;
  }
  if (!fresh.state.empty()) {
    s.state = fresh.state;
  }
  // The bridge identity was decided at allocation and is not restated by a
  // session read.
  if (fresh.bridge_present) {
    s.bridge_present = true;
    s.bridge_id = fresh.bridge_id;
    s.bridge_uid = fresh.bridge_uid;
    s.bridge_lan_host = fresh.bridge_lan_host;
  }
  return s;
}
