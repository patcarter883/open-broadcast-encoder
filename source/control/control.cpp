// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "control/control.h"

#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "httplib.h"

using json = nlohmann::json;

namespace
{
const char* codec_str(codec c) noexcept
{
  switch (c) {
    case codec::h264:
      return "h264";
    case codec::h265:
      return "h265";
    case codec::av1:
      return "av1";
  }
  return "h264";
}

const char* proto_str(output_proto p) noexcept
{
  switch (p) {
    case output_proto::rtmp:
      return "rtmp";
    case output_proto::rtmps:
      return "rtmps";
    case output_proto::srt:
      return "srt";
    case output_proto::rist:
      return "rist";
  }
  return "rtmp";
}

bool post_json(const std::string& host,
               int port,
               const std::string& token,
               const std::string& path,
               const std::string& body,
               std::string& err)
{
  httplib::Client cli(host, port);
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(5, 0);
  cli.set_write_timeout(5, 0);

  httplib::Headers headers;
  if (!token.empty()) {
    headers.emplace("Authorization", "Bearer " + token);
  }

  httplib::Result res = cli.Post(path, headers, body, "application/json");
  if (!res) {
    err = "no response from receiver at " + host + ":" + std::to_string(port)
        + " (" + httplib::to_string(res.error()) + ")";
    return false;
  }
  if (res->status / 100 != 2) {
    err = "receiver returned HTTP " + std::to_string(res->status) + ": "
        + res->body;
    return false;
  }
  return true;
}
}  // namespace

control_client::control_client(std::string host_, int port_, std::string token_)
    : host {std::move(host_)}
    , port {port_}
    , token {std::move(token_)}
{
  secrets::register_secret(token);  // M1.9: never reaches the log panes
}

bool control_client::start(const receiver_control_config& cfg,
                           codec source_codec,
                           std::string& err)
{
  // CONTRACT schema_version 2 (transport profile, 2026-07-18): the receiver
  // is copy-only fan-out — outputs carry no video/audio mode blocks (the
  // fields are gone, not ignored: a v1 body is rejected with invalid_schema).
  json body;
  body["schema_version"] = 2;
  body["session_id"] = cfg.session_id;
  body["source"]["codec"] = codec_str(source_codec);

  json outputs = json::array();
  for (std::size_t i = 0; i < cfg.destinations.size(); ++i) {
    const receiver_destination& d = cfg.destinations[i];
    json o;
    o["id"] = "out" + std::to_string(i);
    o["type"] = proto_str(d.proto);
    o["url"] = d.url;
    if (!d.stream_key.empty()) {
      secrets::register_secret(d.stream_key);  // M1.9
      o["key_or_streamid"] = d.stream_key;
    }
    outputs.push_back(std::move(o));
  }
  body["outputs"] = std::move(outputs);

  return post_json(host, port, token, "/start", body.dump(), err);
}

bool control_client::stop(const std::string& session_id, std::string& err)
{
  json body;
  body["schema_version"] = 2;
  body["session_id"] = session_id;
  return post_json(host, port, token, "/stop", body.dump(), err);
}
