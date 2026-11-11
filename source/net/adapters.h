// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <string>
#include <vector>

// The host's network adapters, as an uplink list needs them: the name the
// operator picks, the IPv4 address the adapter carries, and whether it is up.
//
// Two consumers, one enumeration: the Output pane's picker lists what list()
// returns, and Start resolves each uplink's saved name through miface_for() to
// the value librist's `miface` parameter takes on this platform.
namespace net
{
struct adapter
{
  // Device name on Linux/macOS ("enp13s0", "wwan0"), adapter name on Windows
  // ("Ethernet 2"). Never a librist miface value on Windows: see bind_style.
  std::string name;
  // Dotted quad. Empty when the adapter has no IPv4 address (only IPv6, or
  // down) -- such an adapter cannot carry a leg.
  std::string ipv4;
  bool up = false;
};

// How librist is told which interface a peer leaves by. A device name is what
// forces egress on Linux (SO_BINDTODEVICE) and macOS (IP_BOUND_IF); Windows has
// no device bind and its strong host model makes the source address
// authoritative, so the address is what is passed there.
enum class bind_style
{
  device_name,
  source_address,
};

// The style this build runs on.
#if defined(_WIN32) || defined(_WIN64)
inline constexpr bind_style host_bind_style = bind_style::source_address;
#else
inline constexpr bind_style host_bind_style = bind_style::device_name;
#endif

// One entry per adapter name, in the order the OS reports them: its first IPv4
// address and its up state. An adapter without an IPv4 is still listed, with an
// empty address, so the picker can show why it cannot be used.
auto list() -> std::vector<adapter>;

// True when `text` is a dotted-quad IPv4 literal.
auto is_ipv4_literal(const std::string& text) -> bool;

// The `miface` value for an uplink that names `name`, under `style`:
//  - an adapter name resolves to the device name (device_name) or to that
//    adapter's IPv4 address (source_address);
//  - an IPv4 literal is taken as written -- librist binds the source address,
//    which is also the only thing that forces egress when the leg leaves by an
//    adapter this host does not own;
//  - anything else, and an adapter with no IPv4 under source_address, returns
//    empty. The caller refuses that leg: a leg that fell back to the default
//    route looks bound and is not.
auto miface_for(const std::vector<adapter>& adapters,
                const std::string& name,
                bind_style style) -> std::string;
}  // namespace net
