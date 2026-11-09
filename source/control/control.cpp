// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <chrono>
#include <string>
#include <thread>
#include <utility>

#include "control/control.h"

#include <nlohmann/json.hpp>

#include "control/transcode_rate.h"
#include "lib/httplib_tls.h"

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

// Split the allocation's control_url into the origin httplib wants and the
// per-session path the endpoint hangs off. The URL carries the session path
// (e.g. https://node.example.au/s/s_9f2c), and httplib::Client(origin) discards
// any path it was given, so the two halves must be separated. Shared by /start
// and /status so the two can never disagree about which session they address.
struct control_target
{
  std::string origin;
  std::string path;  // the session path, without the endpoint
};

control_target split_control_url(const std::string& control_url)
{
  control_target t;
  const auto scheme = control_url.find("://");
  const auto path_at =
      control_url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
  t.origin = path_at == std::string::npos ? control_url
                                          : control_url.substr(0, path_at);
  t.path = path_at == std::string::npos ? std::string {}
                                        : control_url.substr(path_at);
  return t;
}
}  // namespace

bool control_client::prepare_hosted_body(const std::string& start_body_json,
                                         codec source_codec,
                                         std::string& out_body,
                                         std::string& err,
                                         std::uint32_t ingest_fps,
                                         int ingest_kbps,
                                         std::uint32_t ingest_width,
                                         std::uint32_t ingest_height,
                                         int source_kbps,
                                         int link_budget_kbps)
{
  json body = json::parse(start_body_json, nullptr, false);
  if (body.is_discarded() || !body.is_object()) {
    err = "allocation returned an unparseable start_body";
    return false;
  }
  // The portal's document, with only the fields it cannot know corrected: the
  // receiver's schema version, the ingest codec and geometry, the standby
  // slate, every transcode gop, and a transcode target that asks for more than
  // this encoder sends. The outputs -- ids, urls, keys, and the opt-in
  // transcode target with its scale and bitrate, as far as it fits -- are the
  // portal's decision and go through untouched (DT-22).
  //
  // The gop is not a decision, it is a duration: the portal states it as two
  // seconds at an assumed 60 fps because the allocate request carries only the
  // POP (DT-22), and it cannot know the ingest rate. Two seconds at 60 is four
  // at the 30 fps this deployment runs -- the ceiling the destination's own
  // guidance says not to exceed -- so it is restated here in frames at the
  // snapped rate this encoder is actually running. The scale stands.
  body["schema_version"] = k_receiver_schema_version;
  body["source"]["codec"] = codec_str(source_codec);

  // The geometry is the encoder's for the same reason the rate is: the portal
  // has never seen the picture. Written as ints because the receiver rejects a
  // non-integer, and zero is the contract's "unknown" -- the receiver then
  // holds a slate at the feed's size rather than guessing at 1080p.
  body["source"]["width"] = static_cast<int>(ingest_width);
  body["source"]["height"] = static_cast<int>(ingest_height);
  body["source"]["fps"] = static_cast<int>(ingest_fps);

  // Every hosted session runs the slate: Stop Encode leaves the session
  // allocated and the node on standby (main.cpp's stop()). A body that already
  // carries a standby object keeps its other members (brand image, text,
  // timeouts) -- only `enabled` is the encoder's to state.
  if (!body["standby"].is_object()) {
    body["standby"] = json::object();
  }
  body["standby"]["enabled"] = true;

  // The ingest rate is the encoder's: it is the only side that knows what it is
  // actually sending, and the portal cannot state it (the allocate request
  // carries only the POP). A body with no ingest object keeps the portal's.
  if (ingest_kbps > 0 && body["ingest"].is_object()) {
    body["ingest"]["bandwidth"] = ingest_kbps;
  }

  if (ingest_fps > 0) {
    const std::uint32_t gop = ingest_fps * 2;
    for (auto& output : body["outputs"]) {
      if (output.contains("transcode") && output["transcode"].is_object()) {
        output["transcode"]["gop"] = gop;
      }
    }
  }

  // The transcode target is derived from what this encoder sends and from what
  // the link carries: the portal sizes it from the plan (or from an explicit
  // destination bitrate, which wins there outright), and has never seen the
  // rate this pipeline runs at, the transport ceiling it declares, or the WAN
  // rate the bridge measured. A target above any of those cannot add detail and
  // would spend uplink the link does not have, so it is lowered to the resolved
  // rate; one that already fits is left exactly as the portal stated it.
  // Nothing known leaves the document untouched. The derivation lives in
  // transcode_rate.h.
  const int transcode_target = transcode_rate::resolve(
      source_kbps, link_budget_kbps, ingest_kbps);
  if (transcode_target > 0) {
    for (auto& output : body["outputs"]) {
      if (!output.contains("transcode") || !output["transcode"].is_object()) {
        continue;
      }
      auto& transcode = output["transcode"];
      int stated = 0;
      if (auto it = transcode.find("bitrate_kbps");
          it != transcode.end() && it->is_number())
      {
        stated = it->get<int>();
      }
      transcode["bitrate_kbps"] = transcode_rate::body_kbps(
          stated, source_kbps, link_budget_kbps, ingest_kbps);
    }
  }

  out_body = body.dump();
  return true;
}

bool control_client::start_hosted(const std::string& control_url,
                                  const std::string& control_token,
                                  const std::string& start_body_json,
                                  codec source_codec,
                                  std::string& err,
                                  std::uint32_t ingest_fps,
                                  int ingest_kbps,
                                  std::uint32_t ingest_width,
                                  std::uint32_t ingest_height,
                                  int source_kbps,
                                  int link_budget_kbps)
{
  // Split the allocation's control_url into the origin httplib wants and the
  // path /start hangs off.
  const control_target target = split_control_url(control_url);
  const std::string& origin = target.origin;
  if (origin.empty()) {
    err = "allocation returned an empty control_url";
    return false;
  }
  const std::string path = target.path + "/start";

  std::string body;
  if (!prepare_hosted_body(start_body_json,
                           source_codec,
                           body,
                           err,
                           ingest_fps,
                           ingest_kbps,
                           ingest_width,
                           ingest_height,
                           source_kbps,
                           link_budget_kbps))
  {
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

  // The session container is created by the allocation, so the receiver is
  // still starting when this runs: a first POST can meet a connection failure
  // or the ingress's 502. Retry those briefly rather than making the operator
  // press Start again.
  httplib::Result res;
  for (int attempt = 0; attempt < 12; ++attempt) {
    res = cli.Post(path, headers, body, "application/json");
    const bool transient =
        !res || res->status == 502 || res->status == 503 || res->status == 504;
    if (!transient) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
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

control_client::receiver_status control_client::status(
    const std::string& control_url, const std::string& control_token)
{
  receiver_status r;
  const control_target target = split_control_url(control_url);
  if (target.origin.empty()) {
    r.error = "empty control_url";
    return r;
  }
  const std::string path = target.path + "/status";

  secrets::register_secret(control_token);  // M1.9
  httplib::Client cli(target.origin);
  // Short timeouts: this runs on the poll loop, which must stay responsive to
  // the encoder starting and to the app exiting. A receiver that needs longer
  // than this is one the next poll will ask again anyway.
  cli.set_connection_timeout(2, 0);
  cli.set_read_timeout(3, 0);
  httplib::Headers headers;
  if (!control_token.empty()) {
    headers.emplace("Authorization", "Bearer " + control_token);
  }

  const auto res = cli.Get(path, headers);
  if (!res) {
    r.error = "no response from the receiver's control plane at "
        + target.origin + path + " (" + httplib::to_string(res.error()) + ")";
    return r;
  }
  r.http_status = res->status;
  if (res->status < 200 || res->status >= 300) {
    r.error = "receiver status failed: HTTP " + std::to_string(res->status);
    return r;
  }
  const json body = json::parse(res->body, nullptr, false);
  if (body.is_discarded() || !body.is_object()
      || !body.contains("is_running") || !body["is_running"].is_boolean())
  {
    // Without an unambiguous boolean there is nothing to act on, and treating
    // a missing field as "not running" would re-post /start on every poll
    // against a receiver that never asked for it.
    r.error = "receiver status carried no is_running boolean";
    return r;
  }
  r.is_running = body["is_running"].get<bool>();
  r.ok = true;
  return r;
}

hold_poll_decision decide_hold_poll(bool status_ok,
                                    bool is_running,
                                    int consecutive_failures)
{
  hold_poll_decision d;
  if (!status_ok) {
    // No answer: keep holding and try again later, but never re-post into an
    // unknown state. The interval doubles so a node that is down for an hour
    // is not asked 720 times.
    int interval = 5;
    for (int i = 0; i < consecutive_failures && interval < 60; ++i) {
      interval *= 2;
    }
    d.next_poll_seconds = interval > 60 ? 60 : interval;
    return d;
  }
  if (!is_running) {
    d.repost_start = true;
  }
  d.next_poll_seconds = 5;
  return d;
}
