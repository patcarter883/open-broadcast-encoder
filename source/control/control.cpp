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

const char* encoder_str(encoder e) noexcept
{
  switch (e) {
    case encoder::amd:
      return "amd";
    case encoder::qsv:
      return "qsv";
    case encoder::nvenc:
      return "nvenc";
    case encoder::software:
      return "software";
  }
  return "software";
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
}

bool control_client::start(const receiver_control_config& cfg,
                           codec source_codec,
                           std::string& err)
{
  json body;
  body["schema_version"] = 1;
  body["session_id"] = cfg.session_id;
  body["source"]["codec"] = codec_str(source_codec);

  json outputs = json::array();
  for (std::size_t i = 0; i < cfg.destinations.size(); ++i) {
    const receiver_destination& d = cfg.destinations[i];
    json o;
    o["id"] = "out" + std::to_string(i);
    o["type"] = proto_str(d.proto);
    o["url"] = d.url;
    o["key_or_streamid"] = d.stream_key;

    json v;
    if (cfg.reencode) {
      v["mode"] = "reencode";
      v["codec"] = codec_str(cfg.video.out_codec);
      v["encoder"] = encoder_str(cfg.video.enc);
      v["bitrate_kbps"] = cfg.video.bitrate;
      v["upscale"] = cfg.video.upscale;
      v["width"] = cfg.video.width;
      v["height"] = cfg.video.height;
    } else {
      // Copy mode: video.codec must equal source.codec (CONTRACT §4).
      v["mode"] = "copy";
      v["codec"] = codec_str(source_codec);
    }
    o["video"] = v;

    json a;
    a["mode"] = "copy";
    a["codec"] = "aac";
    o["audio"] = a;

    outputs.push_back(std::move(o));
  }
  body["outputs"] = std::move(outputs);

  return post_json(host, port, token, "/start", body.dump(), err);
}

bool control_client::stop(const std::string& session_id, std::string& err)
{
  json body;
  body["schema_version"] = 1;
  body["session_id"] = session_id;
  return post_json(host, port, token, "/stop", body.dump(), err);
}
