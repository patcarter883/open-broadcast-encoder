// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <chrono>
#include <vector>

#include "discovery/mdns_parse.h"

// LAN auto-discovery of a rist2rist bridge (prototype). The bridge
// (luci-app-rist2rist) advertises "_obr-rist._udp" over mDNS via OpenWrt's
// umdns; the encoder browses for it here so the operator can leave the RIST
// output host blank instead of typing the router's address.
namespace discovery
{
// Default DNS-SD service type the rist2rist bridge advertises.
inline constexpr const char* k_service = "_obr-rist._udp.local";

// One-shot browse: multicast a PTR query for `service` and collect every
// responder seen within `timeout`. Bridges whose A record was absent get their
// host filled from the responding datagram's source address. Never throws;
// returns an empty vector on any socket error or if nothing answers.
std::vector<bridge> discover_bridges(std::chrono::milliseconds timeout = std::chrono::milliseconds(1500),
                                     const char* service = k_service);
}  // namespace discovery
