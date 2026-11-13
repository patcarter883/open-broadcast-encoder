// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef _WIN32
#  include <sys/types.h>
#else
#  include <cstdint>
using u_int16_t = uint16_t;
#endif

#include "RISTNet.h"
#include "lib/lib.h"
#include "stats/bond_weight.h"
#include "transport/uplink.h"
#include "url/url.h"

using std::string;

class transport
{
public:
  // Builds the RIST sender from the output configuration. Returns false when
  // the configuration is refused (a PSK this build cannot key), so the caller
  // refuses the Start instead of sending a stream that authenticates nothing.
  auto setup_rist_sender(output_config& output_c) -> bool;
  // Returns false when the RIST sender rejected the buffer: either no context
  // is live (initSender never ran, or rist-cpp's own sendData destroyed it
  // after a hard write error) or the write itself failed. The caller must stop
  // sending rather than spin, because rist-cpp's error path emits no log in a
  // release build and every later call fails identically.
  auto send_buffer(const std::vector<uint8_t>& data, u_int16_t connection_id)
      -> bool;
  transport();
  ~transport();
  transport(const transport&) = delete;
  transport& operator=(const transport&) = delete;
  transport(transport&&) = delete;
  transport& operator=(transport&&) = delete;
  void set_log_callback(int (*log_callback)(void*,
                                            enum rist_log_level,
                                            const char*));
  void set_statistics_callback(void (*statistics_callback)(const rist_stats&));
  void set_oob_callback(void (*oob_callback)(const uint8_t*, size_t));

  // The counters libRIST reports for the sender's peer, as the upload probe
  // consumes them: one sample per stats interval, cached as it arrives.
  struct peer_snapshot
  {
    // Grows by one per sample, so a reader can tell a fresh interval from the
    // one it already used.
    std::uint64_t epoch = 0;
    std::uint64_t sent = 0;
    std::uint64_t received = 0;
    std::uint64_t retransmitted = 0;
    std::uint32_t rtt_ms = 0;
    double quality = 0.0;
  };

  // The newest sender-peer sample, cached from the libRIST stats thread.
  // Returns false when the sender has reported nothing yet.
  auto latest_peer_snapshot(peer_snapshot& out) -> bool;

  // Destroys the live sender. rist_destroy() joins libRIST's sender thread, so
  // this belongs on a worker thread with no FLTK lock held -- the probe calls
  // it before and after its ramp.
  void destroy_sender();

private:
  std::unique_ptr<RISTNetSender> rist_sender =
      std::make_unique<RISTNetSender>();
  int (*log_callback)(void*, enum rist_log_level, const char*) = nullptr;
  // Read from the RIST sender thread (stats/OOB dispatch) and written from the
  // main/UI thread (run_loop/stop) — atomic to avoid a data race on the swap.
  std::atomic<void (*)(const rist_stats&)> statistics_callback {nullptr};
  std::atomic<void (*)(const uint8_t*, size_t)> oob_callback {nullptr};
  void stats_cb_func(const rist_stats& stats);

  // The newest sender-peer sample and its epoch, written from the libRIST stats
  // thread and read from whichever thread runs the probe.
  std::mutex peer_snapshot_mtx;
  peer_snapshot peer_latest;
  bool peer_latest_valid = false;

  // One leg of the bond, in the order the uplinks resolved. peer_id is what
  // librist reports in that peer's stats -- the only thing that attributes a
  // sample to a leg, because every leg of a session shares its cname.
  struct leg_state
  {
    std::string label;
    std::string miface;
    bool auto_weight = false;
    std::uint32_t peer_id = 0;
    // The weight the sender gives this leg now, and the share of the rotation
    // it therefore holds.
    std::uint32_t weight = 0;
    // An estimate waiting to be applied, and the numbers behind it. Zero means
    // nothing is waiting.
    std::uint32_t pending_weight = 0;
    std::string pending_note;
  };
  std::vector<leg_state> legs;
  // Guards legs: the stats callback runs on the librist sender thread and the
  // send thread applies what it computed.
  std::mutex legs_mtx;
  // Turns one stats sample into a weight for the leg it belongs to. Runs on the
  // librist sender thread, which holds librist's peer list lock, so it may not
  // call back into librist.
  void estimate_weights(const rist_stats& stats);
  // Applies any pending estimate. Runs on the send thread.
  void apply_pending_weights();
  void oob_cb_func(
      const uint8_t* buf,
      size_t size,
      std::shared_ptr<RISTNetSender::NetworkConnection>& connection,
      rist_peer* peer);
  void log_line(enum rist_log_level level, const std::string& text);
};
