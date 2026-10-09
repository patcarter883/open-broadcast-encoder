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
class control_client
{
public:
  control_client(std::string host, int port, std::string token);

  // Build the CONTRACT POST /start body from cfg (using source_codec as the
  // copy-mode codec / source.codec) and send it. Returns true on HTTP 2xx;
  // otherwise fills err with a human-readable reason.
  bool start(const receiver_control_config& cfg,
             codec source_codec,
             std::string& err);

  // Apply an allocation: POST a body the PORTAL built to the receiver's control
  // plane. BACKPLANE.md:264 is explicit that the encoder sends this "body
  // verbatim to <control_url>/start", so the URL is absolute and may be https
  // (the node proxies it). The fields the portal cannot know are corrected
  // here: schema_version, which the receiver refuses when stale; source.codec,
  // the codec this encoder will actually send; and each transcode gop, which
  // the portal states as two seconds at an assumed 60 fps. Everything else
  // passes through untouched -- including the transcode target, its scale and
  // its bitrate -- and the portal stays the single configuration location
  // (DT-22).
  //
  // The receiver accepts ONLY an exact match on its own schema version, so this
  // must track the receiver, not the portal: when the receiver's contract moved
  // to 4 (av1 target + output scale), an encoder still sending 3 had every
  // /start refused. Defined once here -- a stale literal at each call site is
  // how that happens.
  static constexpr int k_receiver_schema_version = 4;
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
  static bool prepare_hosted_body(const std::string& start_body_json,
                                  codec source_codec,
                                  std::string& out_body,
                                  std::string& err,
                                  std::uint32_t ingest_fps = 0);

  static bool start_hosted(const std::string& control_url,
                           const std::string& control_token,
                           const std::string& start_body_json,
                           codec source_codec,
                           std::string& err,
                           std::uint32_t ingest_fps = 0);

  // POST /stop for the given session.
  bool stop(const std::string& session_id, std::string& err);

private:
  std::string host;
  int port;
  std::string token;
};
