// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bridge/bridge_client.h"

namespace bridge
{
namespace
{
// DNS names are case-insensitive, and an instance name is a DNS label.
std::string folded(const std::string& text)
{
  std::string out = text;
  std::transform(out.begin(),
                 out.end(),
                 out.begin(),
                 [](unsigned char c)
                 { return static_cast<char>(std::tolower(c)); });
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
    result.error =
        "no bridge advertising " + request.bridge_uid + " is on this LAN";
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
    // We hold a token, so the only remaining question is whether this is still
    // the same box. A different fingerprint means it was reset or swapped.
    if (!request.fingerprint.empty() && !published.empty()
        && published != request.fingerprint)
    {
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

  // No token, so the bridge must be virgin. A fingerprint or a claimed flag
  // means somebody already owns it, and claiming it would take it from them.
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

std::string bridge_state_label(const mdns::service& service, bool holds_token)
{
  const std::string fingerprint = service.txt_value(k_txt_fingerprint);

  // What we HOLD is decided first, then what was advertised. Order matters: a
  // manual address carries no TXT at all (the browse found nothing and the
  // typed address stood in), so an absent fingerprint is not evidence of a
  // virgin bridge -- it is no evidence at all, and saying "virgin" there is a
  // lie that sends the operator to Claim a box which will answer
  // already_claimed.
  std::string state = service.instance + " at " + service.address;
  if (holds_token) {
    state += " - claimed (this encoder holds the pair token)";
  } else if (fingerprint.empty()) {
    state += " - no advertisement; the bridge will state its claim";
  } else {
    state += " - claimed elsewhere";
  }
  return state;
}

bridge_client::bridge_client(discover_fn discover, ubus_factory_fn make_ubus)
    : m_discover(std::move(discover))
    , m_make_ubus(std::move(make_ubus))
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
      // Without the token the claim is worthless: the bridge stored a hash and
      // can never hand the plaintext out again.
      fail(outcome, "no_token", "the bridge returned no token for the claim");
      return outcome;
    }
    outcome.new_token = token;
    client->set_token(token);
  }

  // The session's passphrase is reconciled like the rest of the state: always
  // sent, with an empty value meaning "this session is not encrypted". Sending
  // it unconditionally is the point -- skipping the call when a session carries
  // no key would leave the PREVIOUS session's key on the bridge, and the next
  // cleartext leg would then be configured to encrypt with a dead passphrase.
  //
  // It goes in BEFORE the config that restarts the bridge: the bridge assembles
  // its RIST URLs when it (re)starts, and the apply below is what restarts it.
  // A failure is fatal rather than tolerated -- a bridge that carries media in
  // the clear while reporting success is the worst outcome available.
  const auto secret =
      client->set_link_secret(request.link_secret, request.link_secret_aes);
  if (!secret.ok) {
    fail(outcome, secret.error_code, secret.error);
    return outcome;
  }

  // Only apply when there is something to apply. reconcile replaces outputs
  // WHOLESALE, so an empty desired state would wipe a working bridge's outputs.
  const bool should_apply =
      request.desired.is_object() && !request.desired.empty();
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
    outcome.report.managed =
        state.data.value("managed", outcome.report.managed);
  }

  outcome.ok = true;
  return outcome;
}

namespace
{

// Read an integer field, reporting whether it was actually there. "Absent" and
// "zero" are different answers: a leg with no shaper is not a leg with a shaper
// of 0, and the plugin sends null for the former.
bool read_int(const nlohmann::json& j, const char* key, int& value)
{
  if (!j.is_object()) {
    return false;
  }
  const auto it = j.find(key);
  if (it == j.end() || it->is_null() || !it->is_number()) {
    return false;
  }
  value = static_cast<int>(it->get<double>());
  return true;
}

}  // namespace

calibrate_outcome parse_calibration(const ubus_result& result)
{
  calibrate_outcome out;

  // Three different failures that must not collapse into one message, because
  // the operator's next action differs for each: a transport failure is "check
  // the bridge's address", an ACL refusal is "the bridge would not talk to us",
  // and the plugin's own refusal is the bridge answering -- most often because
  // a stream is running, which is the one this feature exists to respect.
  if (result.http_status == 0) {
    out.error = "the bridge did not answer";
    return out;
  }
  if (result.ubus_status != 0) {
    out.error = "the bridge's ubus refused the call";
    return out;
  }
  if (!result.ok) {
    out.error =
        result.error.empty() ? std::string("the bridge refused") : result.error;
    return out;
  }
  if (!result.data.is_object()) {
    out.error = "the bridge's report was not an object";
    return out;
  }

  const nlohmann::json& d = result.data;
  out.ok = true;
  (void)read_int(d, "aggregate_kbps", out.aggregate_kbps);
  out.aggregate_state = d.value("aggregate_state", std::string {});
  out.shaper = d.value("shaper", std::string {});

  const auto legs = d.value("legs", nlohmann::json::array());
  if (!legs.is_array()) {
    return out;
  }
  for (const auto& l : legs) {
    if (!l.is_object()) {
      continue;
    }
    calibrate_leg leg;
    leg.interface_ = l.value("interface", std::string {});
    leg.state = l.value("state", std::string {});
    leg.has_weight = read_int(l, "weight", leg.weight);
    leg.has_measured = read_int(l, "measured_kbps", leg.measured_kbps);
    leg.has_shaper = read_int(l, "shaper_kbps", leg.shaper_kbps);
    leg.has_shaped =
        read_int(l, "shaped_measured_kbps", leg.shaped_measured_kbps);
    leg.has_shaped_failed_at =
        read_int(l, "shaped_failed_at_kbps", leg.shaped_failed_at_kbps);
    leg.shaped_verdict = l.value("shaped_verdict", std::string {});
    leg.has_quality = read_int(l, "quality", leg.quality);
    for (const auto& r : l.value("repeats_kbps", nlohmann::json::array())) {
      if (r.is_number()) {
        leg.repeats_kbps.push_back(static_cast<int>(r.get<double>()));
      }
    }
    out.legs.push_back(std::move(leg));
  }
  return out;
}

std::string format_calibration(const calibrate_outcome& out)
{
  if (!out.ok) {
    return out.error.empty() ? std::string("calibration refused") : out.error;
  }

  std::string text =
      "aggregate " + std::to_string(out.aggregate_kbps) + " kbit/s";
  if (!out.aggregate_state.empty()) {
    text += " (" + out.aggregate_state + ")";
  }
  if (!out.shaper.empty()) {
    // What happened to the shaper that was ALREADY on the bridge -- not the one
    // this run derived, which is per leg below. Shown raw, "shaper none" read
    // as though the derived shaper had failed to land, when it means there was
    // nothing there to put back.
    if (out.shaper == "none") {
      text += ", nothing was shaped before";
    } else if (out.shaper == "restored") {
      text += ", the previous shaper was restored";
    } else if (out.shaper == "applied") {
      // This run installed the shaper it derived from the measurement.
      text += ", a shaper was applied from the measurement";
    } else if (out.shaper == "restore_failed") {
      // The one state the operator must not miss: the WAN was left unshaped.
      text += ", PREVIOUS SHAPER NOT RESTORED";
    } else {
      text += ", existing shaper " + out.shaper;  // unknown state, as given
    }
  }

  for (const auto& l : out.legs) {
    text += "\n" + l.interface_ + ": ";
    if (!l.has_measured) {
      // Distinguish a refused leg from a dead one: the bridge says which, and
      // "refused" is a normal answer (a session is up, or the day's budget is
      // spent) rather than a fault.
      text += l.state.empty() ? std::string("no rate measured")
                              : l.state + " - no rate measured";
      continue;
    }
    text += std::to_string(l.measured_kbps) + " kbit/s";
    if (!l.repeats_kbps.empty()) {
      text += " from [";
      for (size_t i = 0; i < l.repeats_kbps.size(); ++i) {
        text += (i == 0 ? "" : ", ") + std::to_string(l.repeats_kbps[i]);
      }
      text += "]";
    }
    if (l.has_weight) {
      text += ", weight " + std::to_string(l.weight);
    }
    if (!l.has_shaper) {
      // Say it in words. A shaper that was never set is the thing the operator
      // most needs to notice, and "shaper 0" would read as a value.
      text += ", NO SHAPER SET";
    } else {
      text += ", shaper " + std::to_string(l.shaper_kbps);
    }
    if (l.has_shaped) {
      // Held-then-broke, when both are known: the shaper sits between the two
      // numbers, which is the whole confirmation.
      text += "; shaped run ";
      if (l.has_shaped_failed_at) {
        text += std::to_string(l.shaped_measured_kbps) + "->"
            + std::to_string(l.shaped_failed_at_kbps);
      } else {
        text += std::to_string(l.shaped_measured_kbps);
      }
      if (!l.shaped_verdict.empty()) {
        text += " " + l.shaped_verdict;
      }
    }
  }
  return text;
}

calibrate_outcome bridge_client::calibrate(const reconcile_request& request,
                                           std::chrono::milliseconds window)
{
  calibrate_outcome out;

  if (!m_discover) {
    out.error = "no discovery is configured";
    return out;
  }
  const auto found = m_discover(window);
  const auto match = find_bridge(found, request);
  if (!match.found) {
    out.error = match.error;
    return out;
  }
  if (request.known_token.empty()) {
    // The bridge authenticates the pair and `calibrate` is not a method a
    // virgin bridge answers, so name the action rather than relaying a refusal
    // the operator cannot act on.
    out.error = "claim the bridge first - calibration needs a pair token";
    return out;
  }

  auto client = m_make_ubus(ubus_base_url(match.service), request.known_token);
  if (!client) {
    out.error = "no ubus transport";
    return out;
  }
  return parse_calibration(client->call("calibrate", nlohmann::json::object()));
}

}  // namespace bridge
