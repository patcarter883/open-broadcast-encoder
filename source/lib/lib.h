// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class encode;
class transport;
struct ndi_input;
class user_interface;

enum class input_mode : std::uint8_t
{
  testsrc,
  mpegts,
  sdp,
  ndi,
  raw_local,  // uncompressed video+audio from the OBS raw output plugin
  // MC4: a JPEG XS camera on the LAN, ingested over the SAME path the mpegts
  // mode uses (tsparse/tsdemux -> decodebin3 -> svtjpegxsdec). Placed BEFORE
  // `none` so the existing menu user_data indices (which mirror this enum)
  // and every switch case keep their meaning -- the same rule raw_local's
  // addition followed.
  jpegxs_capture,
  none
};

enum class codec : std::uint8_t
{
  h264,
  h265,
  av1
};

enum class encoder : std::uint8_t
{
  amd,
  qsv,
  nvenc,
  software
};

struct buffer_data
{
  size_t buf_size {0};
  std::vector<uint8_t> buf_data;
};

enum class bitrate_source : std::uint8_t
{
  local,
  remote_oob
};

struct __attribute__((packed)) wan_telemetry
{
  uint8_t link_quality;
  uint32_t worst_case_rtt;  // network byte order on the wire
};
static_assert(sizeof(wan_telemetry) == 5,
              "wan_telemetry must be exactly 5 bytes");

struct cumulative_stats
{
  // Guards every member below. Held by the RIST stats thread when writing the
  // deques/aggregates and by readers (UI, scaling logic) when reading them.
  mutable std::mutex mutex;
  std::deque<int> bandwidth;
  std::deque<int> retransmitted_packets;
  std::deque<int> total_packets;
  std::deque<int> encode_bitrate;
  int bandwidth_avg = 0;
  int retransmitted_packets_sum = 0;
  int total_packets_sum = 0;
  int encode_bitrate_avg = 0;
  int current_bitrate = 0;
  double previous_quality = 0.0;
  int wan_quality = 0;
  uint32_t wan_rtt = 0;
  // ABR decision window (bitrate_scale.cpp). Kept with the rest of the scaling
  // state so both entry points share one copy: the local RIST stats path and
  // the remote_oob path are mutually exclusive, but only one of them may be
  // able to move the bitrate at a time.
  int64_t last_decision_ms = 0;
  int64_t last_decrease_ms = 0;
  double window_quality_sum = 0.0;
  int window_quality_count = 0;
};

struct input_config
{
  std::string selected_input;
  input_mode selected_input_mode = input_mode::none;
  // MC4 jpegxs_capture only. `selected_input` stays what it is for mpegts and
  // raw_local -- the LISTEN PORT -- and the camera chosen from the LAN browse
  // (k_cam_service) lives here, because a unicast reader needs both the port it
  // binds and the camera address it routes toward (and filters on). Both empty
  // is the manual fallback (DT-19): bind the wildcard and accept any sender.
  std::string capture_address;
  std::string capture_name;  // the advertised name, for the UI/logs only
};

struct encode_config
{
  codec selected_codec = codec::h264;
  encoder selected_encoder = encoder::software;
  std::atomic<int> bitrate {4300};
  std::atomic<bitrate_source> scaling_source {bitrate_source::local};
  // MPEG-TS packets per mux output buffer = bytes per RIST datagram (n * 188).
  // 7 (=1316 B) is the standard RTP/MPEG-TS payload, but with RIST's RTP/GRE
  // headers that can exceed a low cellular path MTU and get IP-fragmented (one
  // lost fragment drops the whole packet). Lower it (e.g. 6 => 1128 B) so each
  // RIST datagram fits under the link MTU. Applied at the next Start.
  std::atomic<int> mpegts_alignment {7};
};

struct output_config
{
  std::string address = "127.0.0.1:5000";
  std::string host = "127.0.0.1";
  int port = 5000;
  int streams = 1;
  // Recovery buffer floor must leave room for several retransmit rounds over a
  // high-RTT mobile link; reorder hold-off must be a SMALL fraction of it
  // (librist default 15 ms). A large reorder value (was 240 ms, ~= buffer_min)
  // eats the recovery window and disables retransmission.
  int buffer_min = 1000;
  int buffer_max = 5000;
  int rtt_min = 40;
  int rtt_max = 500;
  int reorder_buffer = 30;
  int bandwidth = 6000;
};

// ---------------------------------------------------------------------------
// Receiver control: where the partner open-broadcast-receiver should restream
// the incoming RIST stream, and how. Sent to the receiver over the REST
// control plane on Start (see source/control/control.h and the receiver's
// docs/CONTRACT.md). The codec/encoder enum integer order is the wire contract
// shared with the receiver — do not renumber.
// ---------------------------------------------------------------------------

enum class output_proto : std::uint8_t
{
  rtmp,
  rtmps,
  srt,
  rist
};

struct receiver_destination
{
  output_proto proto = output_proto::rtmp;
  std::string url;
  std::string stream_key;  // RTMP key or SRT streamid
};

struct receiver_control_config
{
  bool enabled = false;  // drive a receiver at all?
  std::string control_host = "127.0.0.1";
  int control_port = 8080;
  std::string token;
  std::string session_id;
  // Transcode is NOT decided here. The portal owns the fan-out and each
  // destination's opt-in transcode target (DT-22), and the encoder sends its
  // `/start` body through verbatim -- see apply_allocation_to_receiver(). A
  // local copy-vs-reencode setting used to live here and was never sent to the
  // receiver: it collected a decision nothing acted on, so it is gone rather
  // than kept alongside the portal's, which would be a second source of truth.
  std::vector<receiver_destination> destinations;
};

// ---------------------------------------------------------------------------
// Bridge control (DT-19, DT-21). The rist2rist bridge on the customer LAN: the
// encoder finds it, claims it ONCE, then applies the configuration it should
// run. The bridge never contacts the backplane and holds no fleet credential.
//
// This is the LAN-side slice: discovery, claim and apply all work with no
// hosted backend, because a claim's token comes FROM the bridge. Where the
// token comes from is the only thing the portal changes.
// ---------------------------------------------------------------------------
struct bridge_control_config
{
  // Manual fallback (DT-19): discovery is a convenience, never a dependency, so
  // an address here is used when the browse finds nothing.
  std::string address;
  std::string bridge_uid;  // the mDNS instance, once found or claimed
  // The pair token. A SECRET: registered with the secrets registry so it cannot
  // reach a log pane, and shown in the UI only as "set"/"not set".
  std::string token;
  std::string listen_url {"rist://0.0.0.0:5000"};  // what the BRIDGE listens on
  std::string forward_to;  // the external ingest node it forwards to
  std::string interface_name {"wan"};  // the bridge's uplink to bond over
  std::string last_error;  // shown in the UI; not persisted
};

// ---------------------------------------------------------------------------
// Hosted control plane (BACKPLANE §2/§4). The encoder is self-host-only until
// this is populated: `backplane_url` + `device_token` are what let it onboard
// as a device and reach the portal at all.
//
// The device token is a SECRET (it authorises everything against the account),
// so it is registered with the secrets registry and shown in the UI only as
// set/not-set.
// ---------------------------------------------------------------------------
struct hosted_config
{
  std::string backplane_url;  // e.g. https://api.backplane.example.au
  std::string device_token;  // SECRET; from the RFC 8628 device flow
  long device_id = 0;  // the backplane's id for this encoder
  // The backplane's row for the bridge this encoder is driving. Learned from
  // the report, and needed to fetch a pair token the portal holds (DT-21 path
  // A).
  long bridge_id = 0;
};

inline std::pair<std::string, int> parse_address(const std::string& addr)
{
  // Note: IPv6 addresses must use the bracketed [host]:port form for the
  // port to parse; a bare IPv6 literal will be treated as host-only below.
  auto colon = addr.find(':');
  if (colon == std::string::npos) {
    if (addr.empty()) {
      return {"127.0.0.1", 5000};
    }
    return {addr, 5000};
  }
  std::string h = addr.substr(0, colon);
  if (h.empty()) {
    h = "127.0.0.1";
  }
  std::string p = addr.substr(colon + 1);
  try {
    int port = std::stoi(p);
    if (port < 1 || port > 65535) {
      return {h, 5000};
    }
    return {h, port};
  } catch (...) {
    return {h, 5000};
  }
}

struct library
{
  library() noexcept;
  ~library();
  library(const library&) = delete;
  library& operator=(const library&) = delete;
  library(library&&) = delete;
  library& operator=(library&&) = delete;

  std::atomic_bool is_running {false};
  std::atomic_bool preview_running {false};
  std::shared_ptr<std::atomic<bool>> run_flag;

  std::vector<std::thread> threads;
  std::thread preview_thread;

  input_config input_cfg;
  encode_config encode_cfg;
  output_config output_cfg;
  receiver_control_config receiver_ctl;
  bridge_control_config bridge_ctl;
  hosted_config hosted;

  cumulative_stats stats;

  // Accessed concurrently from the main thread (run_loop), stop(), and RIST
  // callback threads. C++20 std::atomic<shared_ptr> serialises the swap.
  std::atomic<std::shared_ptr<encode>> encoder_ptr;

  void log_append(const std::string& msg) const;
};

// ---------------------------------------------------------------------------
// Secret hygiene (FIXPLAN M1.9). Stream keys, bearer tokens and PSKs must
// never reach the UI log panes or the transport log. Register every secret as
// soon as it is known; route every log line through redact() before display.
// URLs are loggable by definition — secrets never enter URLs — but librist and
// third-party lines are redacted defensively anyway.
// ---------------------------------------------------------------------------

namespace secrets
{
// Remember a secret value so redact() can mask it. Values shorter than 4
// characters are ignored (masking them would shred normal text). Idempotent.
void register_secret(const std::string& value);

// Return text with every registered secret replaced by "***" and the values
// of secret=/streamid=/psk=/token= URL-style parameters masked.
auto redact(std::string text) -> std::string;
}  // namespace secrets

struct app_context
{
  library lib;
  user_interface* ui = nullptr;
  std::unique_ptr<transport> transporter;
  std::unique_ptr<ndi_input> ndi;
};