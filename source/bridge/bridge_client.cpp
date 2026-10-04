// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "bridge/bridge_client.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bridge
{
namespace
{
// DNS names are case-insensitive, and an instance name is a DNS label.
std::string folded(const std::string& text)
{
  std::string out = text;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

void fail(reconcile_outcome& outcome, std::string code, std::string message)
{
  outcome.error_code = std::move(code);
  outcome.error = std::move(message);
  outcome.report.last_error = outcome.error;
}
}  // namespace

std::string ubus_base_url(const mdns::service& service)
{
  // An address is what we can actually reach; the SRV host name may not resolve
  // from this machine at all.
  const std::string host =
      service.address.empty() ? service.host : service.address;
  if (host.empty()) {
    return {};
  }

  std::string url = "http://" + host;
  const std::string port = service.txt_value(k_txt_api_port);
  if (!port.empty() && port != "80") {
    url += ":" + port;
  }
  return url;
}

find_result find_bridge(const std::vector<mdns::service>& found,
                        const reconcile_request& request)
{
  find_result result;

  if (!request.bridge_uid.empty()) {
    const std::string wanted = folded(request.bridge_uid);
    for (const auto& service : found) {
      if (folded(service.instance) == wanted) {
        result.found = true;
        result.service = service;
        return result;
      }
    }
    result.error_code = "bridge_not_found";
    result.error = "no bridge advertising " + request.bridge_uid + " is on this LAN";
    return result;
  }

  if (found.empty()) {
    result.error_code = "bridge_not_found";
    result.error = "no bridge answered on this LAN";
    return result;
  }

  if (found.size() > 1) {
    // Guessing here would configure the wrong box; the portal must say which.
    result.error_code = "ambiguous_bridge";
    result.error = "several bridges are on this LAN and none was named";
    return result;
  }

  result.found = true;
  result.service = found.front();
  return result;
}

bridge_decision decide(const mdns::service& service,
                       const reconcile_request& request)
{
  bridge_decision decision;
  const std::string published = service.txt_value(k_txt_fingerprint);
  const bool published_claimed = service.txt_value(k_txt_claimed) == "1";

  if (!request.known_token.empty()) {
    // We hold a token, so the only remaining question is whether this is still the
    // same box. A different fingerprint means it was reset or swapped.
    if (!request.fingerprint.empty() && !published.empty() &&
        published != request.fingerprint) {
      decision.error_code = "fingerprint_mismatch";
      decision.error =
          "the bridge was reset or replaced -- it must be claimed again";
      return decision;
    }
    decision.action = bridge_action::apply;
    decision.ok = true;
    decision.token = request.known_token;
    return decision;
  }

  // No token, so the bridge must be virgin. A fingerprint or a claimed flag means
  // somebody already owns it, and claiming it would take it from them.
  if (published_claimed || !published.empty()) {
    decision.error_code = "already_claimed";
    decision.error = "the bridge is already claimed by another controller";
    return decision;
  }

  if (!request.allow_claim) {
    decision.error_code = "not_claimed";
    decision.error = "the bridge is unclaimed and claiming is not allowed here";
    return decision;
  }

  decision.action = bridge_action::claim;
  decision.ok = true;
  return decision;
}

bridge_client::bridge_client(discover_fn discover, ubus_factory_fn make_ubus)
    : m_discover(std::move(discover)), m_make_ubus(std::move(make_ubus))
{
}

reconcile_outcome bridge_client::reconcile(const reconcile_request& request,
                                           std::chrono::milliseconds window)
{
  reconcile_outcome outcome;

  if (!m_discover) {
    fail(outcome, "no_discovery", "no discovery is configured");
    return outcome;
  }

  const auto match = find_bridge(m_discover(window), request);
  if (!match.found) {
    fail(outcome, match.error_code, match.error);
    return outcome;
  }

  const auto& service = match.service;
  outcome.service = service;
  outcome.report.bridge_uid = service.instance;
  outcome.report.address = service.address;
  outcome.report.api_version = service.txt_value(k_txt_api);
  outcome.report.managed = service.txt_value(k_txt_managed) == "1";

  const auto decision = decide(service, request);
  if (!decision.ok) {
    outcome.action = bridge_action::none;
    fail(outcome, decision.error_code, decision.error);
    return outcome;
  }

  if (!m_make_ubus) {
    fail(outcome, "no_transport", "no ubus transport is configured");
    return outcome;
  }

  auto client = m_make_ubus(ubus_base_url(service), decision.token);
  if (client == nullptr) {
    fail(outcome, "no_transport", "no ubus transport is configured");
    return outcome;
  }

  outcome.action = decision.action;

  if (decision.action == bridge_action::claim) {
    const auto claimed = client->claim(request.encoder_uid);
    if (!claimed.ok) {
      fail(outcome, claimed.error_code, claimed.error);
      return outcome;
    }
    const std::string token = claim_token(claimed);
    if (token.empty()) {
      // Without the token the claim is worthless: the bridge stored a hash and can
      // never hand the plaintext out again.
      fail(outcome, "no_token", "the bridge returned no token for the claim");
      return outcome;
    }
    outcome.new_token = token;
    client->set_token(token);
  }

  // Only apply when there is something to apply. reconcile replaces outputs
  // WHOLESALE, so an empty desired state would wipe a working bridge's outputs.
  const bool should_apply = request.desired.is_object() && !request.desired.empty();
  if (should_apply) {
    const auto applied = client->reconcile(request.desired);
    if (!applied.ok) {
      fail(outcome, applied.error_code, applied.error);
      return outcome;
    }
  }

  // Read back what the bridge now says, so the portal reports state rather than
  // intent. Best effort: a failed read does not undo a successful apply.
  const auto state = client->get_config();
  if (state.ok) {
    outcome.report.reported_config = state.data;
    outcome.report.managed = state.data.value("managed", outcome.report.managed);
  }

  outcome.ok = true;
  return outcome;
}

}  // namespace bridge
