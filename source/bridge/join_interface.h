// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <functional>
#include <string>

// The bridge joins a multicast group on ONE interface, named by libRIST's
// `miface=` query parameter. Without it a listener binds the group address but
// never joins the group on any interface, so it receives nothing and the
// failure looks like a silent one-way link rather than a bind error.
//
// The bridge daemon's whitelist accepts `listen_url` as a single string, so the
// join is expressed inside that URL ("rist://@239.1.1.10:5100?miface=eth1")
// rather than as a field of its own.

namespace bridge
{

// The IPv4 multicast host named by a RIST URL -- "rist://@239.1.1.10:5100",
// "rist://239.1.1.10:5100?miface=eth1". Empty when the URL names no multicast
// address (a wildcard bind, a unicast host, a malformed URL).
std::string multicast_group(const std::string& url);

// The value of the URL's own `miface=` parameter, or empty.
std::string multicast_interface_of(const std::string& url);

// Adds `miface=<iface>` to a multicast LISTEN url. A url that names no
// multicast group, an empty interface, or a url that already carries `miface=`
// is returned unchanged: the join belongs to a multicast listener and nowhere
// else, and an interface already written into the url is the operator's own
// choice.
std::string with_multicast_interface(std::string url, const std::string& iface);

// Drops any `miface=` parameter, for the copy of a listen url this encoder
// SENDS to: the join interface names a device on the bridge, and a sender that
// inherited it would bind an interface that does not exist here.
std::string without_multicast_interface(const std::string& url);

// A one-line explanation when `listen_url` is a multicast listen leg carrying
// no join interface: such a listener binds the group without joining it and
// receives nothing while its own state reports it as listening. Empty when the
// leg names an interface or is not a multicast listen leg at all.
std::string join_warning(const std::string& listen_url);

// The interface this host would send to `group` through: the kernel's route
// lookup for the group (RTM_GETROUTE -> RTA_OIF). Empty when there is no route,
// the group is not a multicast address, or the platform has no such lookup.
std::string detect_multicast_interface(const std::string& group);

// Which interface to join on, in order of authority: the interface already in
// the url, then `configured`, then a route lookup for the group, then
// `fallback` (the bridge's configured uplink). Empty when the url has no
// multicast listen leg or nothing could be named -- in which case the url is
// actuated exactly as written.
using detect_fn = std::function<std::string(const std::string& group)>;
std::string resolve_join_interface(const std::string& listen_url,
                                   const std::string& configured,
                                   const std::string& fallback,
                                   const detect_fn& detect = {});

}  // namespace bridge
