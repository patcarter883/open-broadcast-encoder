// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The bandwidth the RIST sender declares. libRIST uses it as a ceiling for the
// recovery ring and spends it on retransmissions as well as on the payload, so
// it is not a statement of the payload rate.

#pragma once

#include <optional>
#include <string>

// Bridged session: the encoder reaches the bridge over Ethernet and the WAN
// budget belongs to the bridge's own leg, derived there from the shaper. The
// ceiling is flat, so it does not move with the payload. At the 5000 ms buffer
// the ring holds ~bandwidth*0.475 packets and the NACK window addresses 32768,
// so 20000 kbps keeps ~9500 packets in range.
inline constexpr int rist_bridged_ceiling_kbps = 20000;

// Retransmission budget over the payload for a session whose own leg is the
// WAN: several Mbit/s of headroom over what the encoder sends.
inline constexpr int rist_direct_headroom_kbps = 5000;

// The ceiling for a sender carrying `payload_kbps`. `measured_upload_kbps` is
// the uplink this host measured, when a measurement exists. The result is never
// below the payload, whatever the measurement says.
auto rist_bandwidth_kbps(int payload_kbps,
                         std::optional<int> measured_upload_kbps,
                         bool bridged) -> int;

// The figure to size a direct sender from. A measurement describes the path to
// one destination, so it applies only while the session sends there: a stored
// figure for another address is not evidence about this one and yields nothing.
auto measured_upload_kbps_for(int measured_kbps,
                              const std::string& measured_destination,
                              const std::string& destination)
    -> std::optional<int>;
