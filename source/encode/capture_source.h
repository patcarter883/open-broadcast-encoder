// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// JPEG XS capture ingest: the addressing half (MC4).
//
// Kept free of GStreamer types on purpose, so the unicast decision that the
// live run depends on is unit-testable without a pipeline (the same split
// raw_format.cpp follows). The reader in capture_input.{h,cpp} is the only
// consumer; nothing here parses mDNS -- that is bridge/mdns.{h,cpp}, reused.
//
// The plan's MC4 was written when the camera node MULTICAST the link. It now
// streams JPEG XS UNICAST to one ingest point (an L2 that drops multicast
// frames measured 4-14% loss; unicast measured 0.00%), so the reader LISTENS on
// the ingest point rather than joining a group. That is why an explicit local
// address matters: a unicast datagram is delivered to one socket, and binding
// the interface that routes to the camera is what makes the encoder that
// socket.

#pragma once

#include <cstdint>
#include <string>

namespace capture
{

// The node's default JPEG XS stream port (jpegxs-arm-port/JXS_PORT). Used when
// the operator has not typed a listen port.
inline constexpr std::uint16_t k_default_port = 5000;

// Parse the listen-port field. Returns `fallback` for anything that is not a
// usable port (empty, non-numeric, out of range) -- a bad field must not fail
// the start, it must fall back, the way raw_local's port parsing does.
std::uint16_t parse_stream_port(const std::string& text,
                                std::uint16_t fallback = k_default_port);

// The local IPv4 address to bind so that a reply to `destination` would leave
// this host on the interface facing it. This is the unicast ingest point the
// camera is sending TO (e.g. "10.50.1.51"). Empty when `destination` is empty,
// is 0.0.0.0, is not a resolvable address, or has no route -- in every one of
// those cases the caller binds the wildcard instead, so a missing route is a
// fallback rather than a failure.
std::string local_address_for(const std::string& destination);

}  // namespace capture
