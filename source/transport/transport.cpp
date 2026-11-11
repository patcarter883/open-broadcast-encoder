// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <format>
#include <functional>
#include <memory>
#include <vector>

#include "transport/transport.h"

#include "RISTNet.h"

using std::string;

transport::transport()
{
  this->rist_sender->statisticsCallback =
      std::bind_front(&transport::stats_cb_func, this);
  this->rist_sender->networkOOBDataCallback =
      std::bind_front(&transport::oob_cb_func, this);
}

// destroySender() joins the RIST sender_thread (the only thread dispatching the
// stats/OOB callbacks), so destroying the sender before clearing the callbacks
// is the race-free teardown order.
transport::~transport()
{
  this->rist_sender->closeAllClientConnections();
  this->rist_sender->destroySender();
  this->statistics_callback = nullptr;
  this->oob_callback = nullptr;
}

void transport::set_log_callback(int (*log_callback_func)(void*,
                                                          enum rist_log_level,
                                                          const char*))
{
  this->log_callback = log_callback_func;
}

void transport::set_statistics_callback(
    void (*statistics_callback_func)(const rist_stats&))
{
  this->statistics_callback = statistics_callback_func;
}

void transport::set_oob_callback(void (*oob_callback_func)(const uint8_t*,
                                                           size_t))
{
  this->oob_callback = oob_callback_func;
}

void transport::stats_cb_func(const rist_stats& stats)
{
  auto cb = this->statistics_callback.load(std::memory_order_acquire);
  if (cb != nullptr) {
    cb(stats);
  }
}

void transport::oob_cb_func(
    const uint8_t* buf,
    size_t size,
    std::shared_ptr<RISTNetSender::NetworkConnection>& /*connection*/,
    rist_peer* /*peer*/)
{
  auto cb = this->oob_callback.load(std::memory_order_acquire);
  if (cb != nullptr) {
    cb(buf, size);
  }
}

auto transport::setup_rist_sender(output_config& output_c) -> bool
{
  if (output_c.streams < 1) {
    output_c.streams = 1;
  }
  output_c.port = std::clamp(output_c.port, 1, 65535);
  if (output_c.bandwidth < 100) {
    output_c.bandwidth = 100;
  }
  if (output_c.host.empty()) {
    output_c.host = "127.0.0.1";
  }

  RISTNetSender::RISTNetSenderSettings my_send_configuration;

  std::vector<std::tuple<string, int>> interface_list_sender;

  for (int i = 0; i < output_c.streams; i = i + 1) {
    // timing-mode=0 (SOURCE) — the librist default, and what the known-good
    // reference uses. NOT 1 (ARRIVAL), NOT 2 (RTC):
    //  - RTC (2): receiver drops every packet until an RTCP SR sets
    //  time_offset,
    //    which we never send (ts_ntp=0) -> no data, no recovery.
    //  - ARRIVAL (1): for retransmitted packets the receiver interpolates the
    //    arrival time and asserts packet_time < next->packet_time
    //    (rist-common.c). The extra retries of the double hop
    //    encoder->rist2rist->receiver violate that invariant and SIGABRT the
    //    receiver (assertion build).
    // SOURCE orders/paces by the monotonic source timestamp librist stamps on
    // each packet (preserved unchanged across the rist2rist relay). Must match
    // on every hop.
    // Bonding topology (FIXPLAN M1.7 / CONTRACT §4): every link targets the
    // SAME host:port — bonded links present as multiple RIST peers on the
    // single session port. The old port fan (port + 2*i) silently lost every
    // link but the first against a spec-compliant single-port receiver and is
    // not supported. (True multi-WAN bonding from one machine additionally
    // needs per-interface binding — the rist2rist hop's miface model; until
    // the encoder grows an interface picker, streams>1 peers share the
    // default route and mainly serve the test rig.)
    // '@' is libRIST's LISTEN form (it binds the address and sets local_port).
    // A sender built from it binds the destination and never transmits, so the
    // prefix is stripped here: this side only ever sends.
    string output_host = output_c.host;
    if (!output_host.empty() && output_host.front() == '@') {
      output_host.erase(0, 1);
    }

    string rist_output_url = std::format(
        "rist://"
        "{}:{}?bandwidth={}&buffer-min={}&buffer-max={}&rtt-min={}&rtt-max={}&"
        "reorder-buffer={}&timing-mode=0",
        output_host,
        output_c.port,
        output_c.bandwidth,
        output_c.buffer_min,
        output_c.buffer_max,
        output_c.rtt_min,
        output_c.rtt_max,
        output_c.reorder_buffer);

    interface_list_sender.emplace_back(rist_output_url, 0);
  }

  my_send_configuration.mLogLevel = RIST_LOG_INFO;
  my_send_configuration.mProfile = RIST_PROFILE_ADVANCED;

  // The direct leg carries the session's PSK (4a). The secret reaches librist
  // through RISTNetSenderSettings::mPSK -- a dedicated field, never a URL
  // parameter (H2) -- and rist-cpp copies it into every peer's secret, so each
  // link of a bond is keyed, bonded links included.
  //
  // This build keys at AES-128 only -- the shipped strength, matching what the
  // hosted service issues. A session that asks for AES-256 is refused here
  // rather than downgraded: a 128-bit key against a 256-bit listener carries no
  // media at all, which on screen reads as "started, nothing arrives".
  //
  // An empty psk leaves the sender exactly as it was: clear. That is what a
  // bridged session needs -- the LAN hop to the bridge is clear by design (the
  // bridge's decryptor rejects anything encrypted with "Malformed packet, rtp
  // flag value..."), and the bridge encrypts its own WAN leg.
  if (!output_c.psk.empty()) {
    if (output_c.psk.size() > 128) {
      log_line(RIST_LOG_ERROR,
               "[rist] PSK rejected: longer than the 128-byte librist secret "
               "field\n");
      return false;
    }
    if (output_c.psk_aes != 0 && output_c.psk_aes != 128) {
      log_line(RIST_LOG_ERROR,
               std::format("[rist] PSK rejected: session asks for AES-{} but "
                           "this build keys at AES-128\n",
                           output_c.psk_aes));
      return false;
    }
    my_send_configuration.mPSK = output_c.psk;
    // The state, never the value.
    log_line(RIST_LOG_INFO,
             std::format("[rist] PSK enabled (AES-128) on {} peer(s)\n",
                         output_c.streams));
  } else {
    log_line(RIST_LOG_INFO, "[rist] PSK: none (clear leg)\n");
  }

  my_send_configuration.mLogSetting->log_cb = log_callback;
  this->rist_sender->initSender(interface_list_sender, my_send_configuration);
  return true;
}

// Emit a line through the log callback the owner installed (main routes it into
// the transport pane, which redacts). Silent when no callback is installed, e.g.
// a harness driving the transport without a UI.
void transport::log_line(enum rist_log_level level, const std::string& text)
{
  if (this->log_callback != nullptr) {
    int (*cb)(void*, enum rist_log_level, const char*) = this->log_callback;
    cb(nullptr, level, text.c_str());
  }
}

auto transport::send_buffer(const std::vector<uint8_t>& data,
                            u_int16_t virt_dst_port) -> bool
{
  // RIST carries ~1.3 kB per packet. Handing libRIST a whole muxer buffer (the
  // appsrc delivers up to 16 kB) made it emit 14-16 kB datagrams: a peer that
  // sizes its receive buffer for standard framing drops them, so the flow
  // authenticates and then carries nothing. Split into 1316-byte payloads,
  // which is the size every RIST peer expects.
  constexpr std::size_t kRistPayload = 1316;
  bool ok = true;
  for (std::size_t offset = 0; offset < data.size(); offset += kRistPayload) {
    const std::size_t chunk = std::min(kRistPayload, data.size() - offset);
    ok = this->rist_sender->sendData(
             data.data() + offset, static_cast<int>(chunk), 0, virt_dst_port)
        && ok;
  }
  return ok;
}
