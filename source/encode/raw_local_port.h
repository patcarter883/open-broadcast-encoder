// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// Which port the OBS raw ingest listens on. Deliberately free of GStreamer
// types, so the decision is pinned by a test without a pipeline -- the same
// arrangement as capture_source.h, whose port decision is tested that way.

#pragma once

#include <cstdint>
#include <string>

namespace raw_local
{
// The OBS raw output's own target: obs-raw-output's OBC_DEFAULT_TARGET is
// "127.0.0.1:9300" and its settings default to it.
inline constexpr std::uint16_t k_default_port = 9300;

// The port to listen on: what the operator configured, or the default when that
// is empty or is not a port at all. The listen-port field is shared with the
// MPEG-TS ingest and once held an NDI device name, so a non-numeric value
// reaching here is a case that happens, not a theoretical one.
auto resolve_port(const std::string& configured) -> std::uint16_t;
}  // namespace raw_local
