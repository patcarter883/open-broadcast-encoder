// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "settings/settings.h"

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

// '@' is libRIST's LISTEN form. An output address is a destination, so the
// prefix never belongs in the stored configuration -- strip it on load so a
// value written by an earlier build (or pasted by hand) cannot come back.
static void strip_listen_prefix(std::string& host, std::string& address)
{
  if (!host.empty() && host.front() == '@') {
    host.erase(0, 1);
  }
  const auto at = address.find("://@");
  if (at != std::string::npos) {
    address.erase(at + 3, 1);
  }
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
    case input_mode::raw_local:
      return "raw_local";
    case input_mode::jpegxs_capture:
      return "jpegxs_capture";
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
  } else if (s == "raw_local") {
    out = input_mode::raw_local;
  } else if (s == "jpegxs_capture") {
    out = input_mode::jpegxs_capture;
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

// One object per uplink: the adapter name and how its weight is decided. The
// array is written empty when no uplink is configured, which is the single
// default-route leg -- not the same as an absent key, and both load to the same
// thing, so an older settings file stays valid.
json uplinks_json(const std::vector<uplink::config>& uplinks)
{
  json array = json::array();
  for (const uplink::config& u : uplinks) {
    array.push_back(json {
        {"name", u.name},
        {"weight_is_auto", u.weight_is_auto},
        {"fixed_weight", u.fixed_weight},
    });
  }
  return array;
}

// Entries that name nothing are dropped: a nameless uplink resolves to no
// adapter and would refuse every Start, so a hand-edited file cannot wedge the
// encoder into a configuration its own picker never produces.
std::vector<uplink::config> uplinks_from_json(const json& array)
{
  std::vector<uplink::config> uplinks;
  for (const json& entry : array) {
    if (!entry.is_object()) {
      continue;
    }
    uplink::config u;
    get_to(entry, "name", u.name);
    if (u.name.empty()) {
      continue;
    }
    get_to(entry, "weight_is_auto", u.weight_is_auto);
    get_to(entry, "fixed_weight", u.fixed_weight);
    if (u.fixed_weight < 1) {
      u.fixed_weight = 1;
    }
    uplinks.push_back(std::move(u));
  }
  return uplinks;
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
      home != nullptr && home[0] != '\0') {
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

  json root;

  root["encoder"] = {
      {"name", lib.encoder_name},
  };

  root["input"] = {
      {"selected_input", in.selected_input},
      {"listen_port", in.listen_port},
      {"selected_input_mode", input_mode_str(in.selected_input_mode)},
      // MC4 jpegxs_capture: the LAN camera chosen in the picker. Persisted so a
      // restart keeps the operator's selection; the browse repopulates the
      // list.
      {"capture_address", in.capture_address},
      {"capture_name", in.capture_name},
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

  // Written back clean: the widget can hold the LISTEN form, and persisting it
  // would restore a sender that binds its own destination on the next start.
  std::string out_address = out.address;
  std::string out_host = out.host;
  strip_listen_prefix(out_host, out_address);
  root["output"] = {
      {"address", out_address},
      {"host", out_host},
      {"port", out.port},
      {"streams", out.streams},
      {"buffer_min", out.buffer_min},
      {"buffer_max", out.buffer_max},
      {"rtt_min", out.rtt_min},
      {"rtt_max", out.rtt_max},
      {"reorder_buffer", out.reorder_buffer},
      {"bandwidth", out.bandwidth},
      // One object per uplink, always written: an empty array is the single
      // default-route leg, which is what the encoder sends without a bond.
      {"uplinks", uplinks_json(out.uplinks)},
  };

  const bridge_control_config& bc = lib.bridge_ctl;
  root["bridge"] = {
      {"address", bc.address},
      {"bridge_uid", bc.bridge_uid},
      {"token", bc.token},
      {"listen_url", bc.listen_url},
      {"listen_miface", bc.listen_miface},
      {"forward_to", bc.forward_to},
      {"interface", bc.interface_name},
      {"encrypt_wan_leg", bc.encrypt_wan_leg},
  };

  const hosted_config& hc = lib.hosted;
  root["hosted"] = {
      {"backplane_url", hc.backplane_url},
      {"device_token", hc.device_token},
      {"device_id", hc.device_id},
      {"bridge_id", hc.bridge_id},
  };

  const std::filesystem::path path = settings_file_path();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    return false;
  }
  // M1.9: the settings file holds stream keys and the receiver token —
  // restrict it to the owner BEFORE the secret content is written (the file
  // exists and is empty at this point). Keychain integration is a later
  // nicety (see README).
  std::filesystem::permissions(
      path,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      std::filesystem::perm_options::replace,
      ec);
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
    if (auto m = j.find("selected_input_mode"); m != j.end() && m->is_string())
    {
      parse_input_mode(m->get<std::string>(), in.selected_input_mode);
    }
    get_to(j, "listen_port", in.listen_port);
    // Migrate a file written before the split, when the port lived in
    // selected_input for every mode that takes one. Only a numeric value can
    // have been a port -- an NDI name must not be adopted as one.
    if (in.listen_port.empty() && in.selected_input_mode != input_mode::ndi
        && !in.selected_input.empty())
    {
      const bool numeric = in.selected_input.find_first_not_of("0123456789")
          == std::string::npos;
      if (numeric) {
        in.listen_port = in.selected_input;
      }
    }
    // Seed the model with the same default the field displays, so the field,
    // the saved file and the reader cannot disagree about which port this input
    // listens on. The field is a view of the model, not a second copy of it.
    if (in.listen_port.empty()) {
      if (in.selected_input_mode == input_mode::raw_local) {
        in.listen_port = "9300";
      } else if (in.selected_input_mode == input_mode::jpegxs_capture) {
        in.listen_port = "5000";
      }
    }
    get_to(j, "capture_address", in.capture_address);
    get_to(j, "capture_name", in.capture_name);
    // The UI has no "None" item (the protocol choice only offers the four real
    // modes), so a hand-edited input_mode::none would leave the widget and the
    // model disagreeing after apply_settings(). Normalise it to the app
    // default.
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
        a != j.end() && a->is_number_integer()) {
      enc.mpegts_alignment.store(std::clamp(a->get<int>(), 1, 7),
                                 std::memory_order_relaxed);
    }
  }

  if (auto it = root.find("output"); it != root.end() && it->is_object()) {
    const json& j = *it;
    output_config& out = lib.output_cfg;
    get_to(j, "address", out.address);
    get_to(j, "host", out.host);
    strip_listen_prefix(out.host, out.address);
    get_to(j, "port", out.port);
    get_to(j, "streams", out.streams);
    get_to(j, "buffer_min", out.buffer_min);
    get_to(j, "buffer_max", out.buffer_max);
    get_to(j, "rtt_min", out.rtt_min);
    get_to(j, "rtt_max", out.rtt_max);
    get_to(j, "reorder_buffer", out.reorder_buffer);
    get_to(j, "bandwidth", out.bandwidth);
    if (auto u = j.find("uplinks"); u != j.end() && u->is_array()) {
      out.uplinks = uplinks_from_json(*u);
    }
  }

  if (auto it = root.find("encoder"); it != root.end() && it->is_object()) {
    std::string name;
    get_to(*it, "name", name);
    if (!name.empty()) {
      lib.encoder_name = std::move(name);
    }
  }

  if (auto it = root.find("bridge"); it != root.end() && it->is_object()) {
    const json& j = *it;
    bridge_control_config& bc = lib.bridge_ctl;
    get_to(j, "address", bc.address);
    get_to(j, "bridge_uid", bc.bridge_uid);
    get_to(j, "token", bc.token);
    get_to(j, "listen_url", bc.listen_url);
    get_to(j, "listen_miface", bc.listen_miface);
    get_to(j, "forward_to", bc.forward_to);
    get_to(j, "interface", bc.interface_name);
    get_to(j, "encrypt_wan_leg", bc.encrypt_wan_leg);
  }

  if (auto it = root.find("hosted"); it != root.end() && it->is_object()) {
    const json& j = *it;
    hosted_config& hc = lib.hosted;
    get_to(j, "backplane_url", hc.backplane_url);
    get_to(j, "device_token", hc.device_token);
    hc.device_id = j.value("device_id", 0L);
    hc.bridge_id = j.value("bridge_id", 0L);
  }

  return true;
}

std::filesystem::path hosted_session_path()
{
  return settings_file_path().parent_path() / "hosted-session.json";
}

bool save_hosted_session(const hosted_session& session)
{
  namespace fs = std::filesystem;
  const fs::path path = hosted_session_path();
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    return false;
  }
  // Contains the once-shown control_token + PSK — owner-only, set before the
  // secret content is written (the file exists and is empty here). Mirrors the
  // settings-file 0600 rule (M1.9).
  fs::permissions(path,
                  fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace,
                  ec);
  file << session.to_json() << '\n';
  return static_cast<bool>(file);
}

std::optional<hosted_session> load_hosted_session()
{
  std::ifstream file(hosted_session_path());
  if (!file) {
    return std::nullopt;
  }
  std::string text((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  hosted_session session = hosted_session::from_json(text);
  if (!session.valid()) {
    return std::nullopt;
  }
  return session;
}

void clear_hosted_session()
{
  std::error_code ec;
  std::filesystem::remove(hosted_session_path(), ec);
}
}  // namespace settings
