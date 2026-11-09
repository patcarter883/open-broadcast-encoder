// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <cstdint>
#include <string>

#include "lib/lib.h"

// control_client drives the partner open-broadcast-receiver over its REST
// control plane (see the receiver's docs/CONTRACT.md). It is the encoder-side
// counterpart of the receiver's control_server. JSON/HTTP details are confined
// to control.cpp so httplib/nlohmann do not leak into the rest of the encoder.
//
// Only the hosted path is here: the address and token of the receiver this
// encoder should start come from the allocation, not from operator input.
class control_client
{
public:
  // Apply an allocation: POST a body the PORTAL built to the receiver's control
  // plane. BACKPLANE.md:264 is explicit that the encoder sends this "body
  // verbatim to <control_url>/start", so the URL is absolute and may be https
  // (the node proxies it). The fields the portal cannot know are corrected
  // here: schema_version, which the receiver refuses when stale; source.codec,
  // the codec this encoder will actually send; each transcode gop, which the
  // portal states as two seconds at an assumed 60 fps; and a transcode bitrate
  // that asks for more than this encoder sends at, than its transport ceiling
  // carries, or than the link was measured to take, which is lowered to the
  // resolved rate (transcode_rate.h).
  // Everything else passes through untouched -- including the transcode scale
  // and any bitrate that already fits -- and the portal stays the single
  // configuration location (DT-22).
  //
  // The receiver accepts ONLY an exact match on its own schema version, so this
  // must track the receiver, not the portal: when the receiver's contract moved
  // to 4 (av1 target + output scale), an encoder still sending 3 had every
  // /start refused. Defined once here -- a stale literal at each call site is
  // how that happens. 5 adds the source geometry and the standby slate.
  static constexpr int k_receiver_schema_version = 5;
  //
  // Static because the hosted path has no host/port pair to construct with --
  // only the URL the allocation returned.
  // The body-construction half of start_hosted, split out so the override is
  // testable without an HTTP server: control_client talks httplib directly and
  // has no injection point, so a fixture is the only way to pin the contract.
  // Keeps json inside control.cpp -- in and out are strings, like every other
  // signature in this header.
  //
  // ingest_fps is the rate this encoder is running at, snapped to a standard
  // broadcast rate (24/25/30/50/60). The portal derives every transcode gop
  // from an assumed 60 fps because it cannot know the ingest's (DT-22: the
  // allocate request carries only the POP), so the gop is restated here as two
  // seconds at the real rate. Zero means "not known yet", and the portal's
  // value is then left alone rather than guessed at.
  //
  // ingest_width/ingest_height are the picture size the encoder has measured on
  // its own video encoder. The portal cannot know it either, and the receiver's
  // standby slate must be the size of the feed that will replace it -- a slate
  // assuming 1080p on a 720p session is a visible defect. They are written as
  // integers (the receiver rejects a non-integer), and 0 is the contract's
  // explicit "unknown", never a guess at a default.
  //
  // standby.enabled is set true here: every hosted session runs the slate,
  // because Stop Encode now leaves the session allocated and the node on
  // standby (see main.cpp's stop()). Any other standby members the portal sent
  // are preserved.
  //
  // source_kbps is the rate this encoder's video encoder is configured to send
  // at. It, with ingest_kbps (the transport ceiling the encoder declares and
  // sizes its RIST ring from) and link_budget_kbps (the aggregate WAN rate the
  // bridge measured at its last successful calibration, 0 when no link has been
  // measured since this launch) bounds every transcode target in the body: a
  // transcode cannot carry more detail than its input, may occupy at most 90%
  // of the transport ceiling, and may not exceed what the link was measured to
  // take. A target inside that bound is the portal's and stands. Zero on every
  // input means unknown, and then the body is left alone rather than guessed
  // at.
  static bool prepare_hosted_body(const std::string& start_body_json,
                                  codec source_codec,
                                  std::string& out_body,
                                  std::string& err,
                                  std::uint32_t ingest_fps = 0,
                                  int ingest_kbps = 0,
                                  std::uint32_t ingest_width = 0,
                                  std::uint32_t ingest_height = 0,
                                  int source_kbps = 0,
                                  int link_budget_kbps = 0);

  static bool start_hosted(const std::string& control_url,
                           const std::string& control_token,
                           const std::string& start_body_json,
                           codec source_codec,
                           std::string& err,
                           std::uint32_t ingest_fps = 0,
                           int ingest_kbps = 0,
                           std::uint32_t ingest_width = 0,
                           std::uint32_t ingest_height = 0,
                           int source_kbps = 0,
                           int link_budget_kbps = 0);

  // The receiver's GET /status, reduced to the one fact the encoder acts on
  // (E4). While a session is held and this encoder is not sending, a receiver
  // that is not running means the node lost its config -- a reboot, an
  // OOM-killed container -- and the stored /start body has to be re-posted.
  //
  // `ok` is deliberately separate from `is_running`: "the receiver says it is
  // idle" and "the receiver did not answer" demand opposite responses (re-post
  // the body vs. back off and try again), and collapsing them would make a
  // node that is merely unreachable look like a node that needs rebuilding on
  // every poll.
  struct receiver_status
  {
    bool ok = false;          // the receiver answered with a usable document
    bool is_running = false;  // it is receiving a live feed
    int http_status = 0;
    std::string error;  // human-readable when ok is false
  };

  static receiver_status status(const std::string& control_url,
                                const std::string& control_token);
};

// What one iteration of the held-session poll should do (E4), as a pure
// decision so every branch is testable without a receiver.
//
// Two rules are load-bearing:
//   - a receiver that is idle is re-posted to; a receiver that did not answer
//     is NOT (re-posting into an unknown state is how a poll loop turns into a
//     permanent retry storm against a node that is rebooting).
//   - repeated failures lengthen the interval (5, 10, 20, 40, then 60 s)
//     instead of hammering every 5 s forever.
struct hold_poll_decision
{
  bool repost_start = false;
  int next_poll_seconds = 5;
};

hold_poll_decision decide_hold_poll(bool status_ok,
                                    bool is_running,
                                    int consecutive_failures);