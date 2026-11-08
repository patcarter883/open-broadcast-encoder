// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#ifndef OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_BACKPLANE_H
#define OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_BACKPLANE_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

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
  std::string control_token;  // shown once; secret
  std::string psk;  // shown once; secret
  int psk_aes = 256;
  std::string start_body_json;  // receiver schema-2 /start body, verbatim

  // The portal's view of the session (E3): "allocated", "running", "ended"...
  // Persisted with the rest so a reattach after a restart can say what it found
  // without depending on the fetch that produced it. Empty means this file
  // predates the field -- not a state of its own.
  std::string state;

  // DT-20.1: the bridge the portal chose for this encoder, if any. Identity
  // ONLY -- the pair token is released separately (POST
  // /v1/bridges/:id/credential), so an allocation never carries a media-plane
  // credential.
  //
  // `bridge_present` is load-bearing: it distinguishes "the portal chose no
  // bridge" (present, null) from "this backplane predates the field" (absent).
  // Collapsing those would let a version skew silently route a bridged session
  // straight to the node, past the bridge the operator configured.
  bool bridge_present = false;
  long bridge_id = 0;
  std::string bridge_uid;
  std::string bridge_lan_host;

  bool valid() const { return !session_id.empty() && !control_url.empty(); }

  std::string to_json() const;
  static hosted_session from_json(const std::string& text);
};

// Result of an allocation attempt.
struct alloc_result
{
  bool ok = false;
  hosted_session session;
  std::string error;  // human-readable on failure
  int http_status = 0;
};

// What a GET /sessions/<id> actually told us. These are the four cases that
// demand different behaviour, not the HTTP statuses that produce them -- 500,
// 502 and a connect timeout are one case (try again), 401 is another (a retry
// will not fix it).
enum class session_probe
{
  found,        // 2xx with a session document
  gone,         // 404: the portal does not have this session any more
  unavailable,  // no answer, 5xx, or a document we cannot read: try again
  unauthorised  // 401/403: the device token was refused
};

// Result of reading a session back by id (E3). `ok` means a usable session
// document was returned; every other case is described by `probe`, so the
// caller's decision is made by the pure table below rather than by re-reading
// an HTTP status at the call site.
struct session_fetch_result
{
  bool ok = false;
  int http_status = 0;
  session_probe probe = session_probe::unavailable;
  std::string state;      // the portal's state word, when it said one
  hosted_session session;  // populated when ok
  std::string error;
};

// What the encoder does about a persisted session when it launches (E3).
enum class reattach_action
{
  reattach,       // restore the target and re-post the stored /start body
  clear_session,  // the session is over: drop the file and say so
  retry,          // keep the file and try again in 15 s
  keep_report     // keep the file; this needs the operator, not another try
};

// Classify a fetch by its HTTP status and whether the body parsed into a
// session. Pure, so the mapping is testable without a server.
session_probe classify_probe(int http_status, bool body_usable);

// The E3 decision table: (what the fetch produced, the state the portal
// reported) -> what the encoder does. Pure, so every branch is unit-tested.
//
// The asymmetry is deliberate: only a session the portal calls running or
// allocated -- or one it returns without any state word at all -- is reattached
// to (a re-posted /start into anything else would resurrect a session that is
// being torn down), only a session the portal calls ended -- or does not have
// at all -- is deleted (deleting a session we merely failed to understand would
// orphan nothing and lose a billable one), and a state this build does not
// recognise keeps the file for the operator.
reattach_action decide_reattach(session_probe probe, const std::string& state);

// Fold a fresh read of the session over the persisted copy (E3). The portal
// restates URLs and state, but the control token and PSK are show-once: a
// later read legitimately omits them, so an absent field must never blank the
// one on disk -- that would leave the encoder unable to talk to a receiver it
// still holds. The persisted value wins wherever the fresh document is silent.
hosted_session merge_session(const hosted_session& persisted,
                             const hosted_session& fresh);

// The media-plane inputs the backplane's destination-bitrate rule needs. The
// rule is min(90% of the link, min(source x codec factor, output ceiling)),
// floored and capped, so it can only size a transcode from the real source rate
// -- without it the portal falls back to the session's plan-clamped
// reservation, which describes the plan and not the stream.
//
// Every field is optional, and "absent" is a meaningful value: an encoder that
// does not know a figure omits it rather than sending an estimate the backplane
// would treat as a measurement.
struct bitrate_inputs
{
  // The encode bitrate this encoder will send at, in kbps: the ceiling of its
  // own adaptive controller, which is the rate the stream was configured for.
  std::optional<std::uint32_t> source_bitrate_kbps;

  // The usable WAN budget the encoder has actually measured end to end, in
  // kbps. The bridge does not yet relay link statistics back over OOB, so no
  // such measurement reaches this client; the field stays empty until one does.
  // Filling it with a derived or assumed figure would turn a guess into a
  // stated fact in the portal's accounting.
  std::optional<std::uint32_t> measured_budget_kbps;
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
  using transport_fn =
      std::function<std::pair<int, std::string>(const std::string& method,
                                                const std::string& path,
                                                const std::string& token,
                                                const std::string& body)>;

  // persist: called with the freshly-allocated hosted_session BEFORE it is
  // used (M2.7). The encoder wires this to write the settings file.
  using persist_fn = std::function<void(const hosted_session&)>;

  backplane_client(std::string base_url,
                   std::string device_token,
                   transport_fn transport = {});

  void set_persist(persist_fn persist) { m_persist = std::move(persist); }

  // Allocate a session (DT-22). The request carries the POP, this encoder's
  // name and the bitrate inputs the portal's transcode sizing needs: the
  // fan-out, recording and ingest overrides come from the transport row the
  // operator configured in the portal, and the caller cannot override them. An
  // encoder with no transport row is refused with 409 `no_transport` — so this
  // fails loudly rather than opening a session with no outputs.
  alloc_result allocate(const std::string& pop,
                        const std::string& encoder_name = {},
                        const bitrate_inputs& bitrates = {});

  // Deallocate (the kill switch / abandon step). The device token authorises
  // it. Returns true on success (or if already gone).
  bool deallocate(const std::string& session_id, std::string& err);

  // Read a session back by id (E3). Used at launch, against the session id
  // persisted at allocation time, to decide whether this encoder still has a
  // live allocation to reattach to. Never allocates: a new allocation would be
  // a new session id and therefore a different video.
  session_fetch_result get_session(const std::string& session_id);

  // Abandon-and-reallocate (M2.7): DELETE the lost/orphaned session, then
  // allocate a fresh one with the same request. One user action.
  alloc_result abandon_and_reallocate(const std::string& lost_session_id,
                                      const std::string& pop,
                                      const std::string& encoder_name = {},
                                      const bitrate_inputs& bitrates = {});

  // One saved destination as the portal sees it (§4). `transcode` is the
  // portal's opt-in target for that output -- empty means copy. DISPLAY ONLY:
  // the decision is the portal's (DT-22), and showing it here must not become a
  // second place it can be configured.
  struct destination_view
  {
    std::string label;
    std::string type;
    std::string transcode;
  };

  struct destinations_result
  {
    bool ok = false;
    std::vector<destination_view> destinations;
    std::string error;
  };

  // Read the account's saved destinations so the operator can see what the host
  // will do ("YT (rtmp) -> transcode H.265") before starting. Visibility only,
  // so a failure is non-fatal: the allocation remains the authority.
  destinations_result list_destinations();

private:
  std::string m_base;  // e.g. https://api.backplane.example.au
  std::string m_device_token;
  transport_fn m_transport;
  persist_fn m_persist;
};

// Build an httplib-backed transport for the backplane endpoints. Defined in
// backplane_httplib.cpp so this header stays free of httplib and links into the
// tests. Registers the device token as a secret, so it cannot reach a log pane.
// The read-only "what the host will do" text, one line per destination, for the
// log pane. Pure, so it is testable without a UI: display wording belongs in
// exactly one place, and an empty list has to read as empty rather than as a
// blank line the operator cannot interpret.
std::string format_destination_list(
    const std::vector<backplane_client::destination_view>& destinations);

backplane_client::transport_fn make_httplib_transport(
    const std::string& base_url, const std::string& device_token);

#endif  // OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_BACKPLANE_H
