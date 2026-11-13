// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <format>
#include <utility>

#include "transport/uplink.h"

#include <librist/urlparam.h>

#include "lib/lib.h"

namespace uplink
{
auto resolve(const std::vector<config>& uplinks,
             const std::vector<net::adapter>& adapters,
             net::bind_style style,
             std::string& error) -> std::vector<leg>
{
  std::vector<leg> legs;
  if (uplinks.empty()) {
    legs.push_back(leg {"default", {}, std::nullopt});
    return legs;
  }

  legs.reserve(uplinks.size());
  for (const config& uplink_config : uplinks) {
    leg one;
    one.label = uplink_config.name;
    one.miface = net::miface_for(adapters, uplink_config.name, style);
    if (one.miface.empty()) {
      error = std::format(
          "uplink '{}': no adapter of that name on this host, and it is not an "
          "IPv4 address",
          uplink_config.name);
      return {};
    }
    // A fixed weight is what the URL states; an auto leg starts from the equal
    // share the controller then moves. Both are numbers in the URL, so librist
    // balances from the first packet and nothing is left implicit.
    const auto weight = uplink_config.weight_is_auto
        ? k_start_weight
        : static_cast<std::uint32_t>(
            uplink_config.fixed_weight < 1 ? 1 : uplink_config.fixed_weight);
    one.weight = weight;
    one.auto_weight = uplink_config.weight_is_auto;
    legs.push_back(std::move(one));
  }
  return legs;
}

auto build_url(const output_config& output_c, const leg& leg,
               int bandwidth_kbps) -> std::string
{
  // timing-mode=0 (SOURCE) -- the librist default, and what the known-good
  // reference uses. NOT 1 (ARRIVAL), NOT 2 (RTC):
  //  - RTC (2): receiver drops every packet until an RTCP SR sets time_offset,
  //    which we never send (ts_ntp=0) -> no data, no recovery.
  //  - ARRIVAL (1): for retransmitted packets the receiver interpolates the
  //    arrival time and asserts packet_time < next->packet_time
  //    (rist-common.c). The extra retries of the double hop
  //    encoder->rist2rist->receiver violate that invariant and SIGABRT the
  //    receiver (assertion build).
  // SOURCE orders/paces by the monotonic source timestamp librist stamps on
  // each packet (preserved unchanged across the rist2rist relay). Must match on
  // every hop.
  //
  // '@' is libRIST's LISTEN form (it binds the address and sets local_port). A
  // sender built from it binds the destination and never transmits, so the
  // prefix is stripped here: this side only ever sends.
  std::string output_host = output_c.host;
  if (!output_host.empty() && output_host.front() == '@') {
    output_host.erase(0, 1);
  }

  std::string url = std::format(
      "rist://"
      "{}:{}?bandwidth={}&buffer-min={}&buffer-max={}&rtt-min={}&rtt-max={}&"
      "reorder-buffer={}&timing-mode=0",
      output_host,
      output_c.port,
      bandwidth_kbps,
      output_c.buffer_min,
      output_c.buffer_max,
      output_c.rtt_min,
      output_c.rtt_max,
      output_c.reorder_buffer);

  if (!leg.miface.empty()) {
    url += std::format("&{}={}", RIST_URL_PARAM_MIFACE, leg.miface);
  }
  if (leg.weight.has_value()) {
    url += std::format("&{}={}", RIST_URL_PARAM_WEIGHT, *leg.weight);
  }
  return url;
}
}  // namespace uplink
