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

  // POST /stop for the given session.
  bool stop(const std::string& session_id, std::string& err);

private:
  std::string host;
  int port;
  std::string token;
};
