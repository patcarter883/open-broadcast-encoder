// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#ifndef OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_DEVICE_AUTH_H
#define OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_DEVICE_AUTH_H

#include <functional>
#include <string>
#include <utility>

#include "backplane/backplane.h"

// RFC 8628 device authorization (BACKPLANE §2) — the encoder's onboarding. The
// encoder asks for a code, the operator approves it in the panel, and the
// encoder polls until it is handed a device token. Everything else in the
// hosted path needs that token, which is why this is where the app stops being
// self-host-only.
//
// The logic sits behind the same injected transport as backplane_client, so the
// wire contract AND the polling rules are unit-testable with no server.
//
// The RFC's §3.5 client rules that actually matter, and are therefore enforced
// here rather than left to the caller:
//   - `authorization_pending` is NOT an error. It is the normal state until the
//     operator approves, and treating it as a failure is the bug that makes
//     onboarding look broken while it is working.
//   - `slow_down` means poll LESS often: the server raises the interval and the
//     client must adopt it. Ignoring it earns a permanent slow_down loop.
//   - the `device_code` is a SECRET — it is the thing that mints the token — so
//     it is registered with the secrets registry and never logged.
namespace backplane
{

struct device_code
{
  std::string device_code;  // secret; polls with this
  std::string user_code;  // shown to the operator, e.g. BCDF-GHJK
  std::string verification_uri;  // where they approve it
  int expires_in = 600;
  int interval = 5;  // seconds between polls; may be RAISED by slow_down

  bool valid() const { return !device_code.empty() && !user_code.empty(); }
};

enum class auth_state
{
  approved,  // a token was issued
  pending,  // keep polling: the operator has not approved yet
  slow_down,  // keep polling, but slower (the interval has been raised)
  denied,
  expired,
  error,
};

struct auth_outcome
{
  auth_state state = auth_state::error;
  std::string token;  // set only when approved; secret
  long device_id = 0;
  std::string error;  // human-readable, never carries the code or token
  int http_status = 0;

  bool approved() const { return state == auth_state::approved; }
  // True while waiting is still worthwhile.
  bool polling() const
  {
    return state == auth_state::pending || state == auth_state::slow_down;
  }
};

class device_auth
{
public:
  // transport: (method, path, bearer_token, body) -> (http_status, body).
  // Status 0 means the request never reached the server. Reuses
  // backplane_client's shape so one httplib transport serves both (the auth
  // endpoints take no bearer token).
  using transport_fn = backplane_client::transport_fn;

  // sleep: seconds -> void. Injected so wait_for_approval() is testable without
  // real delays; the default really sleeps.
  using sleep_fn = std::function<void(int)>;

  explicit device_auth(std::string base_url, transport_fn transport = {});

  void set_sleep(sleep_fn sleep) { m_sleep = std::move(sleep); }

  // POST /v1/auth/device/code. On failure `error` is set and the result is not
  // valid(); the returned code is never logged.
  device_code start(const std::string& name,
                    const std::string& platform,
                    std::string& error);

  // POST /v1/auth/device/token, once. `code` is taken by reference because a
  // slow_down RAISES its interval, which the caller must then wait for.
  auth_outcome poll(device_code& code);

  // Poll until the outcome is terminal, waiting `interval` between attempts and
  // honouring slow_down. `cancel` is polled before each wait so the UI can stop
  // a sign-in the operator abandoned; it defaults to never cancelling.
  auth_outcome wait_for_approval(device_code& code,
                                 const std::function<bool()>& cancel = {});

private:
  std::string m_base;
  transport_fn m_transport;
  sleep_fn m_sleep;
};

}  // namespace backplane

// Build an httplib-backed transport for the auth endpoints. Defined in
// device_auth_httplib.cpp so this header stays free of httplib and links into
// the tests.
backplane::device_auth::transport_fn make_device_auth_transport(
    const std::string& base_url);

#endif  // OPEN_BROADCAST_ENCODER_SOURCE_BACKPLANE_DEVICE_AUTH_H
