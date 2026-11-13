// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/adapters.h"

struct output_config;

// The encoder's uplinks: the interfaces this host may send the bond over.
//
// An empty list is the one leg the encoder has always had -- the default route,
// no `miface` and no `weight` in the URL -- so an operator who never opens the
// picker sends exactly what they sent before. With entries, each leg is a peer
// on the SAME host:port (bonded links are peers of one session; the port fan is
// not supported) pinned to its own adapter.
namespace uplink
{
// One configured uplink. `name` is an adapter name from net::list() (or an IPv4
// literal); it is resolved to a `miface` value at Start, so an adapter that is
// renumbered between runs still binds the same device.
struct config
{
  std::string name;
  // True: the weight follows this leg's measured upload, applied live. False:
  // `fixed_weight` is stated in the URL and never changed.
  bool weight_is_auto = true;
  int fixed_weight = 100;
};

// The weight every auto leg starts at. librist balances from the first packet,
// and the controller needs the leg to carry a share before it can measure one.
inline constexpr std::uint32_t k_start_weight = 50;

// One leg as the sender builds it.
struct leg
{
  // The adapter name, for the per-leg log line ("enp13s0", or "default" for the
  // single implicit leg).
  std::string label;
  // Value for librist's `miface` parameter. Empty leaves the leg on the default
  // route, which is what the single implicit leg does.
  std::string miface;
  // Weight stated in the URL and handed to the peer. Unset for the single
  // implicit leg, whose URL stays byte for byte what it was before bonding.
  std::optional<std::uint32_t> weight;
  // True when `weight` is only the starting share and the controller moves it
  // from this leg's measured upload. False keeps the stated weight for good.
  bool auto_weight = false;
};

// The legs an output configuration sends over, in list order. An empty uplink
// list yields the single default-route leg. Otherwise one leg per uplink; a
// name that resolves to no `miface` value sets `error` and the caller must
// refuse the Start rather than send that leg on the default route.
auto resolve(const std::vector<config>& uplinks,
             const std::vector<net::adapter>& adapters,
             net::bind_style style,
             std::string& error) -> std::vector<leg>;

// One leg's RIST URL: the destination, the recovery parameters, and (for a
// configured leg) the `miface` and `weight` parameters that pin and balance it.
auto build_url(const output_config& output_c, const leg& leg,
               int bandwidth_kbps) -> std::string;
}  // namespace uplink
