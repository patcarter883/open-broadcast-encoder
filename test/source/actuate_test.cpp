// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// ONE Allocate action = allocate, apply the bridge on the LAN, report. These tests
// exist to pin the ORDERING and the failure handling, because those are what make
// the single action safe: the bridge's upstream target is the node the allocator
// picked, so it cannot be configured before the session exists.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "bridge/actuate.h"

using namespace bridge;

namespace
{

hosted_session good_session()
{
  hosted_session s;
  s.session_id = "s_abc";
  s.rist_url = "rist://syd1-a.example.au:5000";
  s.control_url = "https://syd1-a.example.au/s_abc";
  s.control_token = "tok-xyz";
  s.psk = "deadbeef";
  return s;
}

// Records what it was asked for and answers with what each test wants.
struct fakes
{
  bool alloc_ok = true;
  std::string alloc_error = "no transport configured";
  hosted_session session = good_session();

  bool reconcile_ok = true;
  std::string reconcile_error_code = "fingerprint_mismatch";
  std::string reconcile_error = "the bridge was reset since fulfilment";
  // What the bridge reports it is listening on. Default matches the request, so a
  // test that does not care about the read-back still gets a working target.
  std::string reconciled_listen_url = "rist://192.168.8.1:6000";

  bool report_ok = true;
  std::string report_error = "no response from backplane";

  int alloc_calls = 0;
  int reconcile_calls = 0;
  int report_calls = 0;
  reconcile_request seen_request;
  std::string seen_new_token;
  std::string seen_service_instance;

  actuator make()
  {
    return actuator(
        [this](const std::string&) -> alloc_result {
          ++alloc_calls;
          alloc_result r;
          r.ok = alloc_ok;
          r.session = session;
          r.error = alloc_ok ? "" : alloc_error;
          return r;
        },
        [this](const reconcile_request& req, std::chrono::milliseconds)
            -> reconcile_outcome {
          ++reconcile_calls;
          seen_request = req;
          reconcile_outcome o;
          o.ok = reconcile_ok;
          o.action = bridge_action::claim;
          o.new_token = "pair-token-1";
          o.service.instance = "rist2rist-aa:bb:cc:dd:ee:ff";
          o.service.address = "192.168.8.1";
          o.report.bridge_uid = req.bridge_uid;
          o.report.address = "192.168.8.1";
          o.report.api_version = "1";
          o.report.managed = true;
          if (reconcile_ok && !reconciled_listen_url.empty()) {
            o.report.reported_config["listen_url"] = reconciled_listen_url;
          }
          o.error_code = reconcile_error_code;
          o.error = reconcile_error;
          return o;
        },
        [this](const mdns::service& svc, const bridge_report&,
               const std::string& new_token) -> report_result {
          ++report_calls;
          seen_service_instance = svc.instance;
          seen_new_token = new_token;
          report_result r;
          r.ok = report_ok;
          r.bridge_id = 42;
          r.error = report_ok ? "" : report_error;
          return r;
        });
  }
};

actuate_request bridged_request()
{
  actuate_request req;
  req.pop = "syd1";
  req.bridge_uid = "rist2rist-aa:bb:cc:dd:ee:ff";
  req.bridge_address = "192.168.8.1";
  req.listen_url = "rist://192.168.8.1:6000";
  req.encoder_uid = "enc-1";
  return req;
}

}  // namespace

TEST_CASE("a direct allocation applies nothing and sends to the node",
          "[bridge][actuate]")
{
  fakes f;
  actuator a = f.make();
  actuate_request req;
  req.pop = "syd1";  // no bridge_uid: the portal chose a direct chain

  const actuate_outcome out = a.run(req, std::chrono::milliseconds {2000});

  REQUIRE(out.ok);
  CHECK_FALSE(out.bridged);
  CHECK(out.encoder_target == "rist://syd1-a.example.au:5000");
  // "No bridge" is a DECISION: there is nothing to claim, apply or report.
  CHECK(f.reconcile_calls == 0);
  CHECK(f.report_calls == 0);
}

TEST_CASE("a failed allocation stops before touching the bridge",
          "[bridge][actuate]")
{
  fakes f;
  f.alloc_ok = false;
  actuator a = f.make();

  const actuate_outcome out =
      a.run(bridged_request(), std::chrono::milliseconds {2000});

  REQUIRE_FALSE(out.ok);
  CHECK(out.error_code == "allocate_failed");
  CHECK(out.error == "no transport configured");
  // Nothing exists to point a bridge at, so the LAN must not be touched.
  CHECK(f.reconcile_calls == 0);
  CHECK(f.report_calls == 0);
}

TEST_CASE("a bridged allocation points the bridge at the allocated node",
          "[bridge][actuate]")
{
  fakes f;
  actuator a = f.make();

  const actuate_outcome out =
      a.run(bridged_request(), std::chrono::milliseconds {2000});

  REQUIRE(out.ok);
  CHECK(out.bridged);
  REQUIRE(f.reconcile_calls == 1);

  // DT-20 single-source: the bridge forwards to the node the ALLOCATOR picked, not
  // to anything the caller supplied.
  const auto& outputs = f.seen_request.desired.at("outputs");
  REQUIRE(outputs.size() == 1);
  CHECK(outputs.at(0).at("address") == "rist://syd1-a.example.au:5000");
  CHECK(f.seen_request.desired.at("listen_url") == "rist://192.168.8.1:6000");
  CHECK(f.seen_request.bridge_uid == "rist2rist-aa:bb:cc:dd:ee:ff");

  // The ENCODER sends to the bridge. If it kept dialling the node, the bridge would
  // sit in the chain un-used.
  CHECK(out.encoder_target == "rist://192.168.8.1:6000");

  // The claim's token goes to the portal, which is the only place it can live.
  CHECK(f.report_calls == 1);
  CHECK(f.seen_new_token == "pair-token-1");
  CHECK(out.report.bridge_uid == "rist2rist-aa:bb:cc:dd:ee:ff");
  CHECK(out.bridge_id == 42);
  CHECK(out.reported);
  CHECK(out.error_code.empty());
}

TEST_CASE("the encoder uses the listen_url the bridge REPORTED, not the one asked for",
          "[bridge][actuate]")
{
  fakes f;
  // The bridge moved the port. Believing our own request would point the encoder at
  // a port nothing is listening on.
  f.reconciled_listen_url = "rist://192.168.8.1:7007";
  actuator a = f.make();

  const actuate_outcome out =
      a.run(bridged_request(), std::chrono::milliseconds {2000});

  REQUIRE(out.ok);
  CHECK(out.encoder_target == "rist://192.168.8.1:7007");
}

TEST_CASE("a bridged chain with nowhere to send fails rather than bypassing the bridge",
          "[bridge][actuate]")
{
  fakes f;
  f.reconciled_listen_url = "";  // the bridge reported no listen_url
  actuator a = f.make();
  actuate_request req = bridged_request();
  req.listen_url.clear();

  const actuate_outcome out = a.run(req, std::chrono::milliseconds {2000});

  REQUIRE_FALSE(out.ok);
  CHECK_FALSE(out.bridged);
  CHECK(out.error_code == "no_listen_url");
  // Falling back to the node's URL would silently route around the bridge the
  // operator configured -- which is the failure this whole model exists to prevent.
  CHECK(out.encoder_target.empty());
  CHECK(f.report_calls == 0);
}

TEST_CASE("a failed reconcile keeps the live session so the caller can release it",
          "[bridge][actuate]")
{
  fakes f;
  f.reconcile_ok = false;
  actuator a = f.make();

  const actuate_outcome out =
      a.run(bridged_request(), std::chrono::milliseconds {2000});

  REQUIRE_FALSE(out.ok);
  CHECK_FALSE(out.bridged);
  // The session exists and is billable: hiding it behind a generic failure would
  // orphan it.
  CHECK(out.session.session_id == "s_abc");
  CHECK(out.error_code == "fingerprint_mismatch");
  CHECK(f.report_calls == 0);
}

TEST_CASE("a failed report does not invalidate a working chain",
          "[bridge][actuate]")
{
  fakes f;
  f.report_ok = false;
  actuator a = f.make();

  const actuate_outcome out =
      a.run(bridged_request(), std::chrono::milliseconds {2000});

  // The LAN is configured and carrying; only the portal's mirror is stale.
  REQUIRE(out.ok);
  CHECK(out.bridged);
  CHECK_FALSE(out.reported);
  CHECK(out.error_code == "report_failed");
  CHECK(out.encoder_target == "rist://192.168.8.1:6000");
}

TEST_CASE("bridge_desired_config maps the session to the bridge's upstream",
          "[bridge][actuate]")
{
  actuate_request req;
  req.listen_url = "rist://192.168.8.1:6000";
  req.interface_name = "wan";

  const auto desired = bridge_desired_config(req, good_session());

  CHECK(desired.at("listen_url") == "rist://192.168.8.1:6000");
  REQUIRE(desired.at("outputs").size() == 1);
  CHECK(desired.at("outputs").at(0).at("address") == "rist://syd1-a.example.au:5000");
  CHECK(desired.at("outputs").at(0).at("interface") == "wan");
  CHECK(desired.at("outputs").at(0).at("weight") == "1");
}
