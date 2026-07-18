// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "settings/settings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
// ---- enum <-> string -------------------------------------------------------
// Stored as strings (not raw integers) so the file stays human-editable and
// survives any future renumbering of the enums. Unknown strings leave the
// caller's existing value untouched.

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

bool parse_codec(const std::string& s, codec& out) noexcept
{
  if (s == "h264") {
    out = codec::h264;
  } else if (s == "h265") {
    out = codec::h265;
  } else if (s == "av1") {
    out = codec::av1;
  } else {
    return false;
  }
  return true;
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

bool parse_encoder(const std::string& s, encoder& out) noexcept
{
  if (s == "amd") {
    out = encoder::amd;
  } else if (s == "qsv") {
    out = encoder::qsv;
  } else if (s == "nvenc") {
    out = encoder::nvenc;
  } else if (s == "software") {
    out = encoder::software;
  } else {
    return false;
  }
  return true;
}

const char* input_mode_str(input_mode m) noexcept
{
  switch (m) {
    case input_mode::testsrc:
      return "testsrc";
    case input_mode::mpegts:
      return "mpegts";
    case input_mode::sdp:
      return "sdp";
    case input_mode::ndi:
      return "ndi";
    case input_mode::none:
      return "none";
  }
  return "none";
}

bool parse_input_mode(const std::string& s, input_mode& out) noexcept
{
  if (s == "testsrc") {
    out = input_mode::testsrc;
  } else if (s == "mpegts") {
    out = input_mode::mpegts;
  } else if (s == "sdp") {
    out = input_mode::sdp;
  } else if (s == "ndi") {
    out = input_mode::ndi;
  } else if (s == "none") {
    out = input_mode::none;
  } else {
    return false;
  }
  return true;
}

const char* bitrate_source_str(bitrate_source b) noexcept
{
  switch (b) {
    case bitrate_source::local:
      return "local";
    case bitrate_source::remote_oob:
      return "remote_oob";
  }
  return "local";
}

bool parse_bitrate_source(const std::string& s, bitrate_source& out) noexcept
{
  if (s == "local") {
    out = bitrate_source::local;
  } else if (s == "remote_oob") {
    out = bitrate_source::remote_oob;
  } else {
    return false;
  }
  return true;
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

bool parse_proto(const std::string& s, output_proto& out) noexcept
{
  if (s == "rtmp") {
    out = output_proto::rtmp;
  } else if (s == "rtmps") {
    out = output_proto::rtmps;
  } else if (s == "srt") {
    out = output_proto::srt;
  } else if (s == "rist") {
    out = output_proto::rist;
  } else {
    return false;
  }
  return true;
}

// ---- typed getters that tolerate a missing/wrong-typed JSON field ----------
// load() must never throw on a partial or hand-edited file: every field is
// optional and falls back to whatever the config already holds.

template<typename T>
void get_to(const json& j, const char* key, T& out)
{
  auto it = j.find(key);
  if (it != j.end() && !it->is_null()) {
    try {
      out = it->get<T>();
    } catch (...) {  // NOLINT(bugprone-empty-catch) — keep existing value
    }
  }
}

void get_codec(const json& j, const char* key, codec& out)
{
  auto it = j.find(key);
  if (it != j.end() && it->is_string()) {
    parse_codec(it->get<std::string>(), out);
  }
}

void get_encoder(const json& j, const char* key, encoder& out)
{
  auto it = j.find(key);
  if (it != j.end() && it->is_string()) {
    parse_encoder(it->get<std::string>(), out);
  }
}
}  // namespace

namespace settings
{
std::filesystem::path settings_file_path()
{
  namespace fs = std::filesystem;
#ifdef _WIN32
  if (const char* appdata = std::getenv("APPDATA");
      appdata != nullptr && appdata[0] != '\0')
  {
    return fs::path(appdata) / "open-broadcast-encoder" / "settings.json";
  }
#else
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME");
      xdg != nullptr && xdg[0] != '\0')
  {
    return fs::path(xdg) / "open-broadcast-encoder" / "settings.json";
  }
  if (const char* home = std::getenv("HOME");
      home != nullptr && home[0] != '\0')
  {
    return fs::path(home) / ".config" / "open-broadcast-encoder"
        / "settings.json";
  }
#endif
  // Last resort: current working directory.
  return fs::path("open-broadcast-encoder-settings.json");
}

bool save(const library& lib)
{
  const input_config& in = lib.input_cfg;
  const encode_config& enc = lib.encode_cfg;
  const output_config& out = lib.output_cfg;
  const receiver_control_config& rc = lib.receiver_ctl;

  json root;

  root["input"] = {
      {"selected_input", in.selected_input},
      {"selected_input_mode", input_mode_str(in.selected_input_mode)},
  };

  root["encode"] = {
      {"codec", codec_str(enc.selected_codec)},
      {"encoder", encoder_str(enc.selected_encoder)},
      {"bitrate", enc.bitrate.load(std::memory_order_relaxed)},
      {"scaling_source",
       bitrate_source_str(enc.scaling_source.load(std::memory_order_relaxed))},
      {"mpegts_alignment",
       enc.mpegts_alignment.load(std::memory_order_relaxed)},
  };

  root["output"] = {
      {"address", out.address},
      {"host", out.host},
      {"port", out.port},
      {"streams", out.streams},
      {"buffer_min", out.buffer_min},
      {"buffer_max", out.buffer_max},
      {"rtt_min", out.rtt_min},
      {"rtt_max", out.rtt_max},
      {"reorder_buffer", out.reorder_buffer},
      {"bandwidth", out.bandwidth},
  };

  json dests = json::array();
  for (const auto& d : rc.destinations) {
    dests.push_back({
        {"proto", proto_str(d.proto)},
        {"url", d.url},
        {"stream_key", d.stream_key},
    });
  }

  root["receiver"] = {
      {"enabled", rc.enabled},
      {"control_host", rc.control_host},
      {"control_port", rc.control_port},
      {"token", rc.token},
      {"reencode", rc.reencode},
      {"video",
       {
           {"encoder", encoder_str(rc.video.enc)},
           {"codec", codec_str(rc.video.out_codec)},
           {"bitrate", rc.video.bitrate},
           {"upscale", rc.video.upscale},
           {"width", rc.video.width},
           {"height", rc.video.height},
       }},
      {"destinations", dests},
  };

  const std::filesystem::path path = settings_file_path();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    return false;
  }
  file << root.dump(2) << '\n';
  return static_cast<bool>(file);
}

bool load(library& lib)
{
  const std::filesystem::path path = settings_file_path();
  std::ifstream file(path);
  if (!file) {
    return false;
  }

  json root = json::parse(file, nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    return false;
  }

  if (auto it = root.find("input"); it != root.end() && it->is_object()) {
    const json& j = *it;
    input_config& in = lib.input_cfg;
    get_to(j, "selected_input", in.selected_input);
    if (auto m = j.find("selected_input_mode");
        m != j.end() && m->is_string())
    {
      parse_input_mode(m->get<std::string>(), in.selected_input_mode);
    }
    // The UI has no "None" item (the protocol choice only offers the four real
    // modes), so a hand-edited input_mode::none would leave the widget and the
    // model disagreeing after apply_settings(). Normalise it to the app default.
    if (in.selected_input_mode == input_mode::none) {
      in.selected_input_mode = input_mode::testsrc;
    }
  }

  if (auto it = root.find("encode"); it != root.end() && it->is_object()) {
    const json& j = *it;
    encode_config& enc = lib.encode_cfg;
    get_codec(j, "codec", enc.selected_codec);
    get_encoder(j, "encoder", enc.selected_encoder);
    if (auto b = j.find("bitrate"); b != j.end() && b->is_number_integer()) {
      enc.bitrate.store(b->get<int>(), std::memory_order_relaxed);
    }
    if (auto s = j.find("scaling_source"); s != j.end() && s->is_string()) {
      bitrate_source src = enc.scaling_source.load(std::memory_order_relaxed);
      if (parse_bitrate_source(s->get<std::string>(), src)) {
        enc.scaling_source.store(src, std::memory_order_relaxed);
      }
    }
    if (auto a = j.find("mpegts_alignment");
        a != j.end() && a->is_number_integer())
    {
      enc.mpegts_alignment.store(std::clamp(a->get<int>(), 1, 7),
                                 std::memory_order_relaxed);
    }
  }

  if (auto it = root.find("output"); it != root.end() && it->is_object()) {
    const json& j = *it;
    output_config& out = lib.output_cfg;
    get_to(j, "address", out.address);
    get_to(j, "host", out.host);
    get_to(j, "port", out.port);
    get_to(j, "streams", out.streams);
    get_to(j, "buffer_min", out.buffer_min);
    get_to(j, "buffer_max", out.buffer_max);
    get_to(j, "rtt_min", out.rtt_min);
    get_to(j, "rtt_max", out.rtt_max);
    get_to(j, "reorder_buffer", out.reorder_buffer);
    get_to(j, "bandwidth", out.bandwidth);
  }

  if (auto it = root.find("receiver"); it != root.end() && it->is_object()) {
    const json& j = *it;
    receiver_control_config& rc = lib.receiver_ctl;
    get_to(j, "enabled", rc.enabled);
    get_to(j, "control_host", rc.control_host);
    get_to(j, "control_port", rc.control_port);
    get_to(j, "token", rc.token);
    get_to(j, "reencode", rc.reencode);

    if (auto v = j.find("video"); v != j.end() && v->is_object()) {
      const json& vj = *v;
      get_encoder(vj, "encoder", rc.video.enc);
      get_codec(vj, "codec", rc.video.out_codec);
      get_to(vj, "bitrate", rc.video.bitrate);
      get_to(vj, "upscale", rc.video.upscale);
      get_to(vj, "width", rc.video.width);
      get_to(vj, "height", rc.video.height);
    }

    if (auto d = j.find("destinations"); d != j.end() && d->is_array()) {
      std::vector<receiver_destination> dests;
      for (const auto& item : *d) {
        if (!item.is_object()) {
          continue;
        }
        receiver_destination dest;
        if (auto p = item.find("proto"); p != item.end() && p->is_string()) {
          parse_proto(p->get<std::string>(), dest.proto);
        }
        get_to(item, "url", dest.url);
        get_to(item, "stream_key", dest.stream_key);
        dests.push_back(std::move(dest));
      }
      rc.destinations = std::move(dests);
    }
  }

  return true;
}
}  // namespace settings
