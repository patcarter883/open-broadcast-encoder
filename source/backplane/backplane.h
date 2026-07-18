// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#ifndef OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_BACKPLANE_H
#define OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_BACKPLANE_H

#include <functional>
#include <string>

// hosted_session — the credentials a hosted allocation hands back (BACKPLANE
// §4.2). These are the SAME three values a self-hoster types by hand (rist
// URL, control URL, bearer token) plus the PSK — the ecosystem promise
// (§13.1) means the encoder's receiver-control path is identical either way;
// hosted mode only changes where the values come from.
//
// M2.7: the encoder persists this the INSTANT allocation returns — before the
// session is used — so a crash between allocate and first use cannot silently
// orphan the allocation. `start_body_json` is the receiver /start body the
// backplane pre-built; the encoder POSTs it verbatim.
struct hosted_session
{
  std::string session_id;
  std::string rist_url;
  std::string control_url;
  std::string control_token;   // shown once; secret
  std::string psk;             // shown once; secret
  int psk_aes = 256;
  std::string start_body_json; // receiver schema-2 /start body, verbatim

  bool valid() const { return !session_id.empty() && !control_url.empty(); }

  std::string to_json() const;
  static hosted_session from_json(const std::string& text);
};

// Result of an allocation attempt.
struct alloc_result
{
  bool ok = false;
  hosted_session session;
  std::string error;   // human-readable on failure
  int http_status = 0;
};

// backplane_client drives the hosted control plane (BACKPLANE §4): device
// authorization (RFC 8628) then session allocation. HTTP is injected as a
// transport function so the logic is unit-testable without a live backplane;
// the default transport is httplib (see backplane.cpp).
class backplane_client
{
public:
  // transport: (method, path, bearer_token, json_body) -> (http_status, body).
  // A status of 0 means the request never reached the server.
  using transport_fn = std::function<std::pair<int, std::string>(
      const std::string& method,
      const std::string& path,
      const std::string& token,
      const std::string& body)>;

  // persist: called with the freshly-allocated hosted_session BEFORE it is
  // used (M2.7). The encoder wires this to write the settings file.
  using persist_fn = std::function<void(const hosted_session&)>;

  backplane_client(std::string base_url, std::string device_token, transport_fn transport = {});

  void set_persist(persist_fn persist) { m_persist = std::move(persist); }

  // Allocate a session. On success, persist() is invoked with the credentials
  // BEFORE returning them to the caller (persist-before-use).
  alloc_result allocate(const std::string& pop,
                        const std::vector<long>& destination_ids,
                        bool record);

  // Deallocate (the kill switch / abandon step). The device token authorises
  // it. Returns true on success (or if already gone).
  bool deallocate(const std::string& session_id, std::string& err);

  // Abandon-and-reallocate (M2.7): DELETE the lost/orphaned session, then
  // allocate a fresh one with the same request. One user action.
  alloc_result abandon_and_reallocate(const std::string& lost_session_id,
                                      const std::string& pop,
                                      const std::vector<long>& destination_ids,
                                      bool record);

private:
  std::string m_base;          // e.g. https://api.backplane.example.au
  std::string m_device_token;
  transport_fn m_transport;
  persist_fn m_persist;
};

#endif  // OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_BACKPLANE_H
