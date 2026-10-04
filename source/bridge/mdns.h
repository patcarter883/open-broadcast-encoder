// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// mDNS discovery of rist2rist bridges (DT-19).
//
// The bridge advertises itself so the encoder can find it without being told an
// address. This file is the PURE half -- query encoding and response parsing -- so
// the packet handling is unit-testable without a network. A manual address is
// always available as a fallback: discovery must never be a dependency.
//
// The advertisement carries only a FINGERPRINT, never the pair token (DT-21, H2).

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bridge::mdns
{

// What the bridge advertises.
inline constexpr const char* k_service = "_rist2rist._udp.local";
inline constexpr const char* k_multicast_group = "224.0.0.251";
inline constexpr std::uint16_t k_multicast_port = 5353;

// DNS record types this parser cares about.
inline constexpr int k_type_a = 1;
inline constexpr int k_type_ptr = 12;
inline constexpr int k_type_txt = 16;
inline constexpr int k_type_aaaa = 28;
inline constexpr int k_type_srv = 33;

struct service
{
  std::string instance;  // rist2rist-11:22:33:44:55:66
  std::string host;      // the SRV target, e.g. OpenWrt.lan
  std::string address;   // the A/AAAA rdata as text, empty when unresolved
  std::uint16_t port = 0;
  std::map<std::string, std::string> txt;

  // A TXT value, or empty when absent.
  std::string txt_value(const std::string& key) const;
};

// Encode a PTR query for the service. id 0 and no recursion, as mDNS requires.
std::vector<std::uint8_t> encode_query(const std::string& service_name = k_service);

// Parse a response packet into the advertised services. Merges the PTR, SRV, TXT
// and A/AAAA answers that describe the same instance. Returns empty on any
// malformed packet rather than reading past it -- a hostile packet on the LAN must
// not be able to crash the encoder.
std::vector<service> parse_response(const std::vector<std::uint8_t>& packet);

}  // namespace bridge::mdns
