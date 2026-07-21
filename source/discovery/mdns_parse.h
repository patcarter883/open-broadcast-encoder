// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Pure (socket-free) mDNS / DNS-SD wire helpers, split out from discovery.cpp
// so the fiddly packet parsing is unit-testable without touching the network.
namespace discovery
{
// A rist2rist bridge discovered on the LAN.
struct bridge
{
  std::string name;  // DNS-SD instance label (e.g. the router hostname)
  std::string host;  // resolved IPv4 from an A record, or empty if none was
                     // included — the socket layer then fills it from the
                     // datagram's source address.
  int port = 0;      // RIST listen port from the SRV record
  std::string txt;   // TXT key=val pairs joined by ';' (metadata; may be empty)
};

// Build a one-shot mDNS PTR query for a DNS-SD service type such as
// "_obr-rist._udp.local". The QU (unicast-response) bit is set so responders
// reply directly to our ephemeral source port — no need to bind/join 5353.
std::vector<uint8_t> build_ptr_query(std::string_view service_fqdn);

// Parse an mDNS response, appending one `bridge` per SRV record found under
// `service_fqdn`. `host` is filled from a matching A record when the response
// carries one, else left empty for the caller to fill. Returns false only on a
// structurally malformed packet; a valid packet with no matching records
// returns true and leaves `out` unchanged.
bool parse_response(const uint8_t* buf,
                    std::size_t len,
                    std::string_view service_fqdn,
                    std::vector<bridge>& out);
}  // namespace discovery
