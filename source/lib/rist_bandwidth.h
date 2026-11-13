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

// Bridge mode reaches the bridge over the LAN, so the ceiling is a share of that
// interface rather than a figure derived from the stream. The remainder is left
// for everything else on the link.
inline constexpr int rist_bridged_interface_pct = 80;

// The ceiling for a bridged session leaving by an interface of
// `interface_mbps`. An unknown or down link falls back to the flat ceiling: a
// ring sized from a made-up speed is worse than one sized from a constant.
auto rist_bridged_cap_kbps(int interface_mbps) -> int;

// The audio track the pipeline muxes beside the video: fdkaacenc's default.
inline constexpr int rist_audio_kbps = 128;

// MPEG-TS into RIST: 7 x 188-byte packets per 1316-byte payload, plus the
// RTP/UDP/IP headers, as a percentage of the payload.
inline constexpr int rist_transport_overhead_pct = 3;

// Retransmission allowance over the stream rate for a session whose own leg is
// the WAN. libRIST treats ?bandwidth= as payload plus retransmissions, and the
// published guidance for a constrained link is an allowance of 50% to 100% of
// the stream rate; 50% is the floor of that range.
inline constexpr int rist_direct_headroom_pct = 50;

auto rist_direct_headroom_kbps(int stream_kbps) -> int;

// The rate the sender actually puts on the wire for a video bitrate: video plus
// audio plus the transport headers. Sizing and probing both start here, so the
// stream is defined once.
auto rist_stream_rate_kbps(int video_kbps) -> int;

// The ceiling for one bonded leg, from the session's. libRIST splits the
// payload rotation between peers in proportion to their weights, so a weighted
// leg carries its share and is capped for that share; a weight of 0 duplicates
// the whole stream down the leg and is capped for the whole stream. The session
// cap is linear in the stream, so scaling it is the same arithmetic as sizing
// each leg's share from scratch.
auto rist_leg_cap_kbps(int session_cap_kbps, int weight, int total_weight)
    -> int;

// The ceiling for a sender carrying `payload_kbps`. `measured_upload_kbps` is
// the uplink this host measured, when a measurement exists. The result is never
// below the payload, whatever the measurement says.
auto rist_bandwidth_kbps(int payload_kbps,
                         std::optional<int> measured_upload_kbps,
                         bool bridged,
                         int bridged_interface_mbps = 0) -> int;

// The figure to size a direct sender from. A measurement describes the path to
// one destination, so it applies only while the session sends there: a stored
// figure for another address is not evidence about this one and yields nothing.
auto measured_upload_kbps_for(int measured_kbps,
                              const std::string& measured_destination,
                              const std::string& destination)
    -> std::optional<int>;
