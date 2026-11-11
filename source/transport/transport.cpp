// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <format>
#include <functional>
#include <memory>
#include <vector>

#include "transport/transport.h"

#include "RISTNet.h"
#include "net/adapters.h"

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
  // The bond is steered from the same samples the panes render, before the
  // owner's callback sees them.
  estimate_weights(stats);
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

  // One peer per uplink, all to the SAME host:port: bonded links present as
  // multiple RIST peers on the single session port (CONTRACT §4). The old port
  // fan (port + 2*i) silently lost every link but the first against a
  // spec-compliant single-port receiver and is not supported.
  //
  // Each leg's URL carries `miface` (the device name on Linux/macOS, the source
  // address on Windows) and `weight`, so the kernel pins the egress and librist
  // balances the payload. A name that resolves to nothing refuses the Start:
  // librist logs a failed device bind and carries on over the default route, so
  // a silently unbound leg would look like a working one.
  std::string uplink_error;
  std::vector<uplink::leg> resolved = uplink::resolve(
      output_c.uplinks, net::list(), net::host_bind_style, uplink_error);
  if (!uplink_error.empty()) {
    log_line(RIST_LOG_ERROR,
             std::format("[bond] Start refused: {}\n", uplink_error));
    return false;
  }
  // An empty uplink list is the single default-route leg this encoder has
  // always sent; `streams` repeats it, which is the test rig's old fan.
  if (output_c.uplinks.empty()) {
    resolved.resize(static_cast<std::size_t>(output_c.streams),
                    resolved.front());
  }

  interface_list_sender.reserve(resolved.size());
  for (std::size_t i = 0; i < resolved.size(); ++i) {
    const uplink::leg& leg = resolved[i];
    interface_list_sender.emplace_back(
        uplink::build_url(output_c, leg),
        static_cast<int>(leg.weight.value_or(0)));
    // librist states the bind it makes ("Binding socket to <device>"); this
    // line states what was asked of it, per leg, so a leg that fell back to the
    // default route is visible as a missing miface rather than as bad video.
    log_line(RIST_LOG_INFO,
             std::format(
                 "[bond] leg {}/{} {}: {}{}\n",
                 i + 1,
                 resolved.size(),
                 leg.label,
                 leg.miface.empty() ? std::string("default route")
                                    : std::format("miface={}", leg.miface),
                 leg.weight.has_value() ? std::format(" weight={}", *leg.weight)
                                        : std::string()));
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
                         resolved.size()));
  } else {
    log_line(RIST_LOG_INFO, "[rist] PSK: none (clear leg)\n");
  }

  my_send_configuration.mLogSetting->log_cb = log_callback;
  this->rist_sender->initSender(interface_list_sender, my_send_configuration);

  // The peer ids librist assigned, in uplink order: a stats sample carries the
  // peer id and the session's cname, and every leg of a bond shares that cname,
  // so the id is the only thing that attributes a sample to its leg.
  {
    std::lock_guard<std::mutex> guard(legs_mtx);
    legs.clear();
    legs.reserve(resolved.size());
    for (std::size_t i = 0; i < resolved.size(); ++i) {
      leg_state state;
      state.label = resolved[i].label;
      state.miface = resolved[i].miface;
      state.auto_weight = resolved[i].auto_weight;
      state.weight = resolved[i].weight.value_or(0);
      state.peer_id = this->rist_sender->getPeerId(i);
      legs.push_back(std::move(state));
    }
  }
  return true;
}

// Emit a line through the log callback the owner installed (main routes it into
// the transport pane, which redacts). Silent when no callback is installed,
// e.g. a harness driving the transport without a UI.
void transport::log_line(enum rist_log_level level, const std::string& text)
{
  if (this->log_callback != nullptr) {
    int (*cb)(void*, enum rist_log_level, const char*) = this->log_callback;
    cb(nullptr, level, text.c_str());
  }
}

void transport::estimate_weights(const rist_stats& stats)
{
  if (stats.stats_type != RIST_STATS_SENDER_PEER) {
    return;
  }
  const rist_stats_sender_peer& sample = stats.stats.sender_peer;
  std::lock_guard<std::mutex> guard(legs_mtx);
  // One leg has nothing to balance: its share is the whole rotation whatever
  // the number says, and changing the weight would only churn the URL state.
  if (legs.size() < 2) {
    return;
  }
  std::uint64_t total_weight = 0;
  for (const leg_state& leg : legs) {
    total_weight += leg.weight;
  }
  if (total_weight == 0) {
    return;
  }
  for (leg_state& leg : legs) {
    if (leg.peer_id != sample.peer_id || !leg.auto_weight) {
      continue;
    }
    const bond_weight::estimate est = bond_weight::estimate_leg(
        bond_weight::leg_sample {
            sample.bandwidth, sample.rtt, sample.sent, sample.retransmitted},
        static_cast<double>(leg.weight) / static_cast<double>(total_weight));
    if (est.weight == leg.weight) {
      return;
    }
    const double attempts =
        static_cast<double>(sample.sent + sample.retransmitted);
    const double retx_percent = attempts == 0.0
        ? 0.0
        : 100.0 * static_cast<double>(sample.retransmitted) / attempts;
    leg.pending_weight = est.weight;
    leg.pending_note = std::format("est {:.0f} kbps (rtt {} ms, retx {:.2f}%)",
                                   est.available_kbps,
                                   sample.rtt,
                                   retx_percent);
    return;
  }
}

void transport::apply_pending_weights()
{
  // librist emits a sender's stats with its peer list lock held, so a weight
  // applied from inside the stats callback deadlocks on that lock. The estimate
  // is computed there and applied here, on the send thread.
  for (;;) {
    std::uint32_t peer_id = 0;
    std::uint32_t weight = 0;
    std::string label;
    std::string note;
    {
      std::lock_guard<std::mutex> guard(legs_mtx);
      leg_state* ready = nullptr;
      for (leg_state& leg : legs) {
        if (leg.pending_weight != 0) {
          ready = &leg;
          break;
        }
      }
      if (ready == nullptr) {
        return;
      }
      peer_id = ready->peer_id;
      weight = ready->pending_weight;
      label = ready->label;
      note = ready->pending_note;
      ready->pending_weight = 0;
    }
    if (!this->rist_sender->setPeerWeight(peer_id, weight)) {
      log_line(RIST_LOG_ERROR,
               std::format(
                   "[bond] {}: weight {} refused by librist\n", label, weight));
      continue;
    }
    {
      std::lock_guard<std::mutex> guard(legs_mtx);
      for (leg_state& leg : legs) {
        if (leg.peer_id == peer_id) {
          leg.weight = weight;
          break;
        }
      }
    }
    log_line(RIST_LOG_INFO,
             std::format("[bond] {}: {} -> weight {}\n", label, note, weight));
  }
}

auto transport::send_buffer(const std::vector<uint8_t>& data,
                            u_int16_t virt_dst_port) -> bool
{
  apply_pending_weights();
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
