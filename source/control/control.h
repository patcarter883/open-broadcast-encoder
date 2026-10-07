// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

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
  // (the node proxies it). Only the two fields the portal cannot know are
  // overridden -- schema_version, which the receiver refuses when stale, and
  // source.codec, the codec this encoder will actually send. Every output
  // therefore passes through untouched, including its opt-in transcode target,
  // and the portal stays the single configuration location (DT-22).
  //
  // Static because the hosted path has no host/port pair to construct with --
  // only the URL the allocation returned.
  static bool start_hosted(const std::string& control_url,
                           const std::string& control_token,
                           const std::string& start_body_json,
                           codec source_codec,
                           std::string& err);

  // POST /stop for the given session.
  bool stop(const std::string& session_id, std::string& err);

private:
  std::string host;
  int port;
  std::string token;
};
