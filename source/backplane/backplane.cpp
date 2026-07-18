// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "backplane/backplane.h"

#include <utility>
#include <vector>

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
  // start_body is stored as parsed JSON so the file stays readable.
  if (!start_body_json.empty()) {
    j["start_body"] = json::parse(start_body_json, nullptr, false);
  }
  return j.dump(2);
}

hosted_session hosted_session::from_json(const std::string& text)
{
  hosted_session s;
  const json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return s;
  }
  s.session_id = j.value("session_id", "");
  s.rist_url = j.value("rist_url", "");
  s.control_url = j.value("control_url", "");
  s.control_token = j.value("control_token", "");
  s.psk = j.value("psk", "");
  s.psk_aes = j.value("psk_aes", 256);
  if (j.contains("start_body")) {
    s.start_body_json = j.at("start_body").dump();
  }
  return s;
}

namespace
{
hosted_session parse_session(const json& body)
{
  hosted_session s;
  if (!body.contains("session")) {
    return s;
  }
  const json& sess = body.at("session");
  s.session_id = sess.value("id", "");
  s.rist_url = sess.value("rist_url", "");
  s.control_url = sess.value("control_url", "");
  s.control_token = sess.value("control_token", "");
  s.psk = sess.value("psk", "");
  s.psk_aes = sess.value("psk_aes", 256);
  if (sess.contains("start_body")) {
    s.start_body_json = sess.at("start_body").dump();
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

alloc_result backplane_client::allocate(const std::string& pop,
                                        const std::vector<long>& destination_ids,
                                        bool record)
{
  alloc_result r;
  if (!m_transport) {
    r.error = "no transport configured";
    return r;
  }
  json req;
  req["schema_version"] = 1;
  if (!pop.empty()) {
    req["pop"] = pop;
  }
  req["destination_ids"] = destination_ids;
  req["record"] = record;

  const auto [status, body] = m_transport("POST", "/api/v1/sessions",
                                          m_device_token, req.dump());
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

bool backplane_client::deallocate(const std::string& session_id, std::string& err)
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
    const std::vector<long>& destination_ids,
    bool record)
{
  std::string err;
  // Best-effort release of the orphan; a failure here (e.g. it was already
  // reaped) must not block getting a working session.
  deallocate(lost_session_id, err);
  return allocate(pop, destination_ids, record);
}
