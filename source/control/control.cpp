// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <string>
#include <utility>

#include "control/control.h"

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
  // CONTRACT schema_version 3 (opt-in transcode tier, 2026-10-03): the receiver
  // is still copy-only fan-out by default — an output with no `transcode` block
  // is copied byte-for-byte — and the video/audio mode blocks of the old decode
  // tier remain gone, not ignored. A v1 or v2 body is rejected with
  // invalid_schema (there is no compatibility shim), so this version MUST track
  // the receiver's contract; see docs/CONTRACT.md.
  json body;
  body["schema_version"] = 3;
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

bool control_client::prepare_hosted_body(const std::string& start_body_json,
                                         codec source_codec,
                                         std::string& out_body,
                                         std::string& err)
{
  json body = json::parse(start_body_json, nullptr, false);
  if (body.is_discarded() || !body.is_object()) {
    err = "allocation returned an unparseable start_body";
    return false;
  }
  // The portal's document, with only the two fields it cannot know corrected.
  // The outputs -- ids, urls, keys and any opt-in transcode target -- are the
  // portal's decision and go through untouched (DT-22).
  body["schema_version"] = 3;
  body["source"]["codec"] = codec_str(source_codec);
  out_body = body.dump();
  return true;
}

bool control_client::start_hosted(const std::string& control_url,
                                  const std::string& control_token,
                                  const std::string& start_body_json,
                                  codec source_codec,
                                  std::string& err)
{
  // Split the allocation's control_url into the origin httplib wants and the
  // path /start hangs off. The URL carries the per-session path (e.g.
  // https://node.example.au/s/s_9f2c), and httplib::Client(origin) discards any
  // path it was given, so the two halves must be separated here.
  const auto scheme = control_url.find("://");
  const auto path_at =
      control_url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
  const std::string origin = path_at == std::string::npos
      ? control_url
      : control_url.substr(0, path_at);
  const std::string path =
      (path_at == std::string::npos ? std::string {}
                                    : control_url.substr(path_at))
      + "/start";
  if (origin.empty()) {
    err = "allocation returned an empty control_url";
    return false;
  }

  std::string body;
  if (!prepare_hosted_body(start_body_json, source_codec, body, err)) {
    return false;
  }

  secrets::register_secret(control_token);  // M1.9
  httplib::Client cli(origin);
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(8, 0);
  cli.set_write_timeout(5, 0);
  httplib::Headers headers;
  if (!control_token.empty()) {
    headers.emplace("Authorization", "Bearer " + control_token);
  }

  httplib::Result res = cli.Post(path, headers, body, "application/json");
  if (!res) {
    err = "no response from the receiver's control plane at " + origin + path
        + " (" + httplib::to_string(res.error()) + ")";
    return false;
  }
  if (res->status < 200 || res->status >= 300) {
    err = "receiver refused the start body: HTTP " + std::to_string(res->status)
        + " " + res->body;
    return false;
  }
  return true;
}

bool control_client::stop(const std::string& session_id, std::string& err)
{
  json body;
  // /stop does not enforce schema_version (CONTRACT §5, deliberately lenient so
  // a stuck encoder can always halt the stream), but we send the current
  // version anyway so both halves of the control plane agree.
  body["schema_version"] = 3;
  body["session_id"] = session_id;
  return post_json(host, port, token, "/stop", body.dump(), err);
}
