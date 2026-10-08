// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// obr-cam-browse -- the LAN "discovered" view (MC5).
//
// A browser cannot do mDNS, so the panel cannot browse the LAN itself. This is
// the small LAN-side helper the plan allows (MC5.2): it reuses the encoder's
// EXISTING DNS-SD parser -- bridge/mdns.{h,cpp} and bridge/discovery.{h,cpp},
// the same translation units the bridge browser uses -- to browse
// `_obr-cam._udp`, and exposes the result as JSON over HTTP on localhost so the
// authenticated panel (running in the operator's browser, on the same LAN) can
// list cameras that have not been registered yet.
//
// This is the "one parser, two service types" rule (MC4.2/MC5.1): not a third
// parser. The two traps that cost real time are carried over in the reused code
// and must not be reintroduced here:
//   * SO_REUSEADDR alone, never SO_REUSEPORT -- in bridge/discovery.cpp.
//   * DNS labels are length-prefixed, so the service type is never searched for
//     as a literal byte string -- in bridge/mdns.cpp.
//   * a malformed record refuses the WHOLE packet by design (MC5.3); when an
//     advertisement vanishes, suspect the packet before the parser.
//
// Discovery is a LISTING, never a control channel (DT-23.3): this helper only
// reads the LAN. Adopting a discovered camera is a separate, authenticated
// write to the backplane (POST /api/v1/capture/sources/adopt) -- it never
// configures the node.
//
// mDNS carries presence; the node's own HTTP API carries durable identity. The
// advertised TXT (MC0.3) has no sensor id, so for each advert the helper probes
// the node's GET /api/sources at the advertised api_port to obtain the STABLE
// source_uid (DT-23.2). If that probe fails the advert is still reported, with
// the instance as a fallback identity -- presence without identity is still
// useful, and a browse must never be a dependency (DT-19).
//
// Usage:
//   obr-cam-browse                 # HTTP helper on 127.0.0.1:8787
//   obr-cam-browse --once          # browse once, print JSON, exit
//   obr-cam-browse --bind 0.0.0.0 --port 8787
//   obr-cam-browse --window 2500   # browse window, milliseconds

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "bridge/discovery.h"
#include "bridge/mdns.h"
#include "httplib.h"

namespace
{
using nlohmann::json;

constexpr const char* k_default_bind = "127.0.0.1";
constexpr int k_default_port = 8787;
constexpr int k_default_window_ms = 1500;
// Long enough for a Pi's Python stdlib HTTP server to answer, short enough that
// an unreachable node does not stall the whole browse.
constexpr int k_probe_timeout_s = 2;

std::string txt_or(const bridge::mdns::service& svc,
                   const std::string& key,
                   const std::string& fallback)
{
  const auto value = svc.txt_value(key);
  return value.empty() ? fallback : value;
}

// A JSON value for a TXT key, or null when absent (so the panel can tell "not
// reported" from an empty string).
json txt_json(const bridge::mdns::service& svc, const std::string& key)
{
  const auto value = svc.txt_value(key);
  return value.empty() ? json(nullptr) : json(value);
}

// Ask the node itself for its durable identity and live health. This is what
// makes an adopted row key on the SAME source_uid the node reports with
// (DT-23.2), instead of a throwaway discovery id.
json probe_node(const std::string& address, const std::string& api_port)
{
  if (address.empty() || api_port.empty()) {
    return json(nullptr);
  }

  httplib::Client client(address, std::atoi(api_port.c_str()));
  client.set_connection_timeout(k_probe_timeout_s, 0);
  client.set_read_timeout(k_probe_timeout_s, 0);

  const auto res = client.Get("/api/sources");
  if (!res || res->status != 200) {
    return json(nullptr);
  }

  json parsed = json::parse(res->body, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return json(nullptr);
  }
  return parsed;
}

// One discovered camera: the raw advert merged with whatever the node's API
// could add.
json to_record(const bridge::mdns::service& svc)
{
  const std::string api_port =
      txt_or(svc, "api_port", std::to_string(svc.port));
  const json node = probe_node(svc.address, api_port);

  json identity = json::object();
  json health = json(nullptr);
  json transport = json(nullptr);
  json source = json(nullptr);
  if (node.is_object()) {
    identity = node.value("identity", json::object());
    health = node.contains("health") ? node["health"] : json(nullptr);
    transport = node.contains("transport") ? node["transport"] : json(nullptr);
    source = node.contains("source") ? node["source"] : json(nullptr);
  }

  // The stable sensor id wins when the node told us; the instance is the honest
  // fallback when it did not.
  std::string uid = identity.value("source_uid", std::string {});
  const std::string identity_source = uid.empty() ? "advert" : "node-api";
  if (uid.empty()) {
    uid = svc.instance;
  }

  // The descriptor the node advertises and the one its API returns are the SAME
  // definition (MC2.1); the API copy is preferred only because it is richer.
  const json desc = source.is_object() ? source : json::object();

  return json {
      {"instance", svc.instance},
      {"host", svc.host},
      {"address", svc.address},
      {"service_port", svc.port},
      {"source_uid", uid},
      {"identity_source", identity_source},
      {"name", desc.value("name", txt_or(svc, "name", svc.instance))},
      {"codec", desc.value("codec", txt_or(svc, "codec", std::string {}))},
      {"resolution",
       desc.value("resolution", txt_or(svc, "resolution", std::string {}))},
      {"fps",
       desc.contains("fps") && !desc["fps"].is_null() ? desc["fps"]
                                                      : txt_json(svc, "fps")},
      {"sampling",
       desc.value("sampling", txt_or(svc, "sampling", std::string {}))},
      {"api_port", api_port.empty() ? json(nullptr) : json(api_port)},
      {"api_version", txt_json(svc, "api_version")},
      {"model", identity.value("model", std::string {})},
      {"sensor", identity.value("sensor", std::string {})},
      {"transport", transport},
      {"health", health},
      {"reachable", node.is_object()},
  };
}

json browse(int window_ms)
{
  const auto found = bridge::discover(std::chrono::milliseconds(window_ms),
                                      bridge::mdns::k_cam_service);

  json sources = json::array();
  for (const auto& svc : found) {
    sources.push_back(to_record(svc));
  }

  return json {
      {"ok", true},
      {"schema_version", 1},
      {"service", bridge::mdns::k_cam_service},
      {"window_ms", window_ms},
      {"sources", sources},
  };
}

void add_cors(httplib::Response& res)
{
  // The panel is served from the backplane's origin and calls this helper on
  // the operator's own machine, so the request is cross-origin. The helper
  // exposes only a read of the public, unauthenticated LAN listing (DT-23.3),
  // so a permissive origin is correct here rather than a hole.
  res.set_header("Access-Control-Allow-Origin", "*");
  res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
  res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

int run_server(const std::string& bind_addr, int port)
{
  httplib::Server server;

  // Every handler sets the CORS header itself (add_cors). It must NOT also be a
  // server default: that emits the header TWICE, and a duplicate
  // Access-Control-Allow-Origin is rejected by the browser as an invalid CORS
  // response ("Failed to fetch") even though the value is `*`.
  server.Options(".*",
                 [](const httplib::Request&, httplib::Response& res)
                 {
                   add_cors(res);
                   res.status = 204;
                 });

  server.Get("/health",
             [](const httplib::Request&, httplib::Response& res)
             {
               add_cors(res);
               res.set_content(
                   json {{"ok", true}, {"service", bridge::mdns::k_cam_service}}
                       .dump(),
                   "application/json");
             });

  server.Get("/discover",
             [](const httplib::Request& req, httplib::Response& res)
             {
               int window = k_default_window_ms;
               if (req.has_param("window")) {
                 window = std::atoi(req.get_param_value("window").c_str());
                 if (window < 200 || window > 8000) {
                   window = k_default_window_ms;
                 }
               }
               add_cors(res);
               res.set_content(browse(window).dump(2), "application/json");
             });

  server.Get("/",
             [](const httplib::Request&, httplib::Response& res)
             {
               add_cors(res);
               res.set_content("obr-cam-browse: GET /discover\n", "text/plain");
             });

  std::cout << "obr-cam-browse: serving " << bridge::mdns::k_cam_service
            << " browse on http://" << bind_addr << ":" << port << "\n"
            << std::flush;

  if (!server.listen(bind_addr, port)) {
    std::cerr << "obr-cam-browse: could not bind " << bind_addr << ":" << port
              << "\n";
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv)
{
  std::string bind_addr = k_default_bind;
  int port = k_default_port;
  int window_ms = k_default_window_ms;
  bool once = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&](std::string& out) -> bool
    {
      if (i + 1 >= argc) {
        std::cerr << "obr-cam-browse: " << arg << " needs a value\n";
        return false;
      }
      out = argv[++i];
      return true;
    };
    if (arg == "--once") {
      once = true;
    } else if (arg == "--bind") {
      if (!next(bind_addr))
        return 2;
    } else if (arg == "--port") {
      std::string v;
      if (!next(v))
        return 2;
      port = std::atoi(v.c_str());
    } else if (arg == "--window") {
      std::string v;
      if (!next(v))
        return 2;
      window_ms = std::atoi(v.c_str());
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "usage: obr-cam-browse [--once] [--bind ADDR] [--port N] "
                   "[--window MS]\n";
      return 0;
    } else {
      std::cerr << "obr-cam-browse: unknown argument " << arg << "\n";
      return 2;
    }
  }

  if (window_ms < 200 || window_ms > 8000) {
    window_ms = k_default_window_ms;
  }

  if (once) {
    std::cout << browse(window_ms).dump(2) << "\n";
    return 0;
  }
  return run_server(bind_addr, port);
}
