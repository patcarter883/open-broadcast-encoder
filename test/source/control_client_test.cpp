// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <string>

#include "control/control.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "encode/fps_snap.h"

using nlohmann::json;

namespace
{
// A body shaped exactly like Allocator::allocate() returns, deliberately left
// at schema_version 2. That skew is the case worth pinning: the receiver
// rejects v1 and v2 outright with 400 invalid_schema, so a verbatim
// pass-through of an older backplane's document is a guaranteed failure. The
// first output is copy-only and the second opted into the transcode tier.
const char* kPortalBody = R"({
  "schema_version": 2,
  "session_id": "s_9f2c",
  "source": {"codec": "h264"},
  "outputs": [
    {"id": "out1", "type": "rtmp", "url": "rtmp://a.example/live2", "key_or_streamid": "KEY1"},
    {"id": "out2", "type": "rtmp", "url": "rtmp://b.example/live2", "key_or_streamid": "KEY2",
     "transcode": {"codec": "h265"}}
  ]
})";
}  // namespace

TEST_CASE("a hosted start body corrects only what the portal cannot know",
          "[hosted][control]")
{
  std::string out;
  std::string err;
  REQUIRE(control_client::prepare_hosted_body(
      kPortalBody, codec::h265, out, err, 30, 0, 1920, 1080));
  REQUIRE(err.empty());

  const json body = json::parse(out);

  // The portal may lag the receiver's contract; the encoder must never ship a
  // stale version. The receiver accepts only an exact match, so this pins the
  // version that actually goes on the wire -- currently 5 (source geometry +
  // standby slate). If the receiver's contract moves, this must move with
  // control_client::k_receiver_schema_version or every /start is refused.
  REQUIRE(body["schema_version"] == 5);
  // The portal can only guess the ingest codec. The encoder knows.
  REQUIRE(body["source"]["codec"] == "h265");
  REQUIRE(body["session_id"] == "s_9f2c");

  // v5 geometry: the portal has never seen the picture, so the encoder states
  // it. Integers, because the receiver rejects a non-integer with 400.
  REQUIRE(body["source"]["width"] == 1920);
  REQUIRE(body["source"]["height"] == 1080);
  REQUIRE(body["source"]["fps"] == 30);
  REQUIRE(body["source"]["width"].is_number_integer());
  REQUIRE(body["source"]["height"].is_number_integer());
  REQUIRE(body["source"]["fps"].is_number_integer());

  // Every hosted session runs the standby slate: Stop Encode holds the session
  // rather than releasing it, so the receiver must have something to put on
  // air.
  REQUIRE(body["standby"]["enabled"] == true);
  REQUIRE(body["standby"]["enabled"].is_boolean());

  // Everything else is the portal's decision (DT-22) and survives verbatim.
  REQUIRE(body["outputs"].size() == 2);
  REQUIRE(body["outputs"][0]["id"] == "out1");
  REQUIRE(body["outputs"][0]["type"] == "rtmp");
  REQUIRE(body["outputs"][0]["url"] == "rtmp://a.example/live2");
  REQUIRE(body["outputs"][0]["key_or_streamid"] == "KEY1");
  // ABSENT means copy, so the copy-only output must not GAIN a block.
  REQUIRE_FALSE(body["outputs"][0].contains("transcode"));
  // And the opted-in one keeps the target the portal chose.
  REQUIRE(body["outputs"][1]["transcode"]["codec"] == "h265");
}

TEST_CASE("an existing standby object is switched on, never duplicated",
          "[hosted][control]")
{
  // The portal may already send a partial standby object (brand image, caption,
  // timeouts). The encoder's only decision is that the slate is ON; the rest is
  // the portal's and must survive, and the object must not be replaced
  // wholesale or appear twice in the document.
  const char* portal = R"({
    "schema_version": 2,
    "session_id": "s_9f2c",
    "source": {"codec": "h264"},
    "standby": {"brand_image": "/usr/share/obr/brand.png",
                "text": "studio standby", "bitrate_kbps": 1500},
    "outputs": [{"id": "out1", "type": "rtmp", "url": "rtmp://a.example/live2"}]
  })";
  std::string out;
  std::string err;
  REQUIRE(control_client::prepare_hosted_body(
      portal, codec::h264, out, err, 30, 0, 1280, 720));

  const json body = json::parse(out);
  REQUIRE(body["standby"].is_object());
  REQUIRE(body["standby"].size() == 4);
  REQUIRE(body["standby"]["enabled"] == true);
  REQUIRE(body["standby"]["brand_image"] == "/usr/share/obr/brand.png");
  REQUIRE(body["standby"]["text"] == "studio standby");
  REQUIRE(body["standby"]["bitrate_kbps"] == 1500);

  // The geometry is stated for the 720p ingest, so the slate is 720p too.
  REQUIRE(body["source"]["width"] == 1280);
  REQUIRE(body["source"]["height"] == 720);
}

TEST_CASE(
    "the source codec the encoder sends is its own, not the portal's default",
    "[hosted][control]")
{
  std::string out;
  std::string err;
  // The portal hard-codes h264; an AV1 or H.265 ingest must still be declared
  // truthfully, because the receiver picks its parser and its
  // rtmp_codec_unsupported gate from this value.
  for (const auto& [in, expected] : {std::pair {codec::h264, "h264"},
                                     std::pair {codec::h265, "h265"},
                                     std::pair {codec::av1, "av1"}})
  {
    REQUIRE(control_client::prepare_hosted_body(kPortalBody, in, out, err));
    REQUIRE(json::parse(out)["source"]["codec"] == expected);
  }
}

TEST_CASE("the transcode gop follows the ingest rate, not the portal's guess",
          "[hosted][control]")
{
  // The allocate request carries only the POP (DT-22), so the portal states
  // every transcode gop as two seconds at an assumed 60 fps: 120 frames. Two
  // seconds at 60 is four at the 30 fps this deployment runs, which is the
  // ceiling the destination's own guidance says not to exceed. The encoder
  // knows the real rate -- measured and snapped to a standard rate before it
  // gets here -- so it restates the gop and leaves the rest alone.
  const char* portal = R"({
    "schema_version": 2,
    "session_id": "s_9f2c",
    "ingest": {"bandwidth": 6000},
    "source": {"codec": "h264"},
    "outputs": [
      {"id": "out1", "type": "rtmp", "url": "rtmp://a.example/live2"},
      {"id": "out2", "type": "rtmp", "url": "rtmp://b.example/live2",
       "transcode": {"codec": "h265", "bitrate_kbps": 6000, "gop": 120}}
    ]
  })";
  std::string out;
  std::string err;

  // 30 fps: two seconds is 60 frames.
  REQUIRE(
      control_client::prepare_hosted_body(portal, codec::h265, out, err, 30));
  {
    const json body = json::parse(out);
    REQUIRE(body["outputs"][1]["transcode"]["gop"] == 60);
    // Only the gop moves: the portal still owns the target and the bitrate.
    REQUIRE(body["outputs"][1]["transcode"]["codec"] == "h265");
    REQUIRE(body["outputs"][1]["transcode"]["bitrate_kbps"] == 6000);
    // A copy-only output must not gain a block.
    REQUIRE_FALSE(body["outputs"][0].contains("transcode"));
  }

  // 25 fps: 50 frames.
  REQUIRE(
      control_client::prepare_hosted_body(portal, codec::h265, out, err, 25));
  REQUIRE(json::parse(out)["outputs"][1]["transcode"]["gop"] == 50);

  // Rate not known yet: the portal's value stands rather than being guessed at.
  REQUIRE(control_client::prepare_hosted_body(portal, codec::h265, out, err));
  REQUIRE(json::parse(out)["outputs"][1]["transcode"]["gop"] == 120);

  // The encoder states the ingest rate: the portal's placeholder is replaced,
  // and the outputs' own bitrates stay the portal's decision.
  REQUIRE(control_client::prepare_hosted_body(
      portal, codec::h265, out, err, 30, 7000));
  {
    const json body = json::parse(out);
    REQUIRE(body["ingest"]["bandwidth"] == 7000);
    REQUIRE(body["outputs"][1]["transcode"]["bitrate_kbps"] == 6000);
  }
  // An unknown rate leaves the portal's value alone.
  REQUIRE(control_client::prepare_hosted_body(portal, codec::h265, out, err));
  REQUIRE(json::parse(out)["ingest"]["bandwidth"] == 6000);
}

TEST_CASE("a transcode target the recipe states is run as stated",
          "[hosted][control]")
{
  // The chain recipe owns the transcode target: a destination's bitrate_kbps is
  // the operator's decision for that link, and the body carries it. The encoder
  // runs it as stated, so the published rate matches the configuration that was
  // reviewed; a figure that does not fit the uplink is a recipe defect to fix
  // in the backplane, not something to rewrite mid-stream.
  const char* portal = R"({
    "schema_version": 2,
    "session_id": "s_9f2c",
    "ingest": {"bandwidth": 20000},
    "source": {"codec": "h264"},
    "outputs": [
      {"id": "out1", "type": "rtmp", "url": "rtmp://a.example/live2"},
      {"id": "out2", "type": "rtmp", "url": "rtmp://b.example/live2",
       "transcode": {"codec": "h265", "bitrate_kbps": 29860, "gop": 120}},
      {"id": "out3", "type": "rtmp", "url": "rtmp://c.example/live2",
       "transcode": {"codec": "h264", "bitrate_kbps": 5250}}
    ]
  })";
  std::string out;
  std::string err;

  // fps, transport ceiling, geometry, source rate, measured link budget.
  REQUIRE(control_client::prepare_hosted_body(
      portal, codec::h265, out, err, 30, 20000, 1920, 1080, 7000, 0));
  {
    const json body = json::parse(out);
    // Stated by the recipe: run as stated, even above what is being sent.
    REQUIRE(body["outputs"][1]["transcode"]["bitrate_kbps"] == 29860);
    REQUIRE(body["outputs"][2]["transcode"]["bitrate_kbps"] == 5250);
    // The rest of the block is still the portal's.
    REQUIRE(body["outputs"][1]["transcode"]["codec"] == "h265");
    REQUIRE(body["outputs"][1]["transcode"]["gop"] == 60);
    // A copy-only output never gains a block.
    REQUIRE_FALSE(body["outputs"][0].contains("transcode"));
  }

  // Neither a source rate nor a measurement rewrites a stated target.
  REQUIRE(control_client::prepare_hosted_body(
      portal, codec::h265, out, err, 30, 20000, 1920, 1080, 40000, 0));
  REQUIRE(json::parse(out)["outputs"][1]["transcode"]["bitrate_kbps"] == 29860);
  REQUIRE(control_client::prepare_hosted_body(
      portal, codec::h265, out, err, 30, 20000, 1920, 1080, 0, 0));
  REQUIRE(json::parse(out)["outputs"][1]["transcode"]["bitrate_kbps"] == 29860);
}

TEST_CASE("a target the recipe omits is sized from the measured link",
          "[hosted][control]")
{
  // An opted-in transcode the recipe left without a target: the bridge's WAN
  // measurement is the only statement of what the link will take, so it
  // outranks both the sending rate and the transport ceiling.
  const char* portal = R"({
    "schema_version": 2,
    "session_id": "s_9f2c",
    "outputs": [
      {"id": "out1", "type": "rtmp", "url": "rtmp://a.example/live2"},
      {"id": "out2", "type": "rtmp", "url": "rtmp://b.example/live2",
       "transcode": {"codec": "h264", "gop": 120}},
      {"id": "out3", "type": "rtmp", "url": "rtmp://c.example/live2",
       "transcode": {"codec": "h265", "bitrate_kbps": 3000, "gop": 120}}
    ]
  })";
  std::string out;
  std::string err;

  SECTION("the measured budget fills the omitted target")
  {
    // fps, ceiling, geometry, source, measured 4 200: the link wins.
    REQUIRE(control_client::prepare_hosted_body(
        portal, codec::h264, out, err, 30, 20000, 1920, 1080, 8000, 4200));
    const json body = json::parse(out);
    REQUIRE(body["outputs"][1]["transcode"]["bitrate_kbps"] == 4200);
    // A target the recipe states is its own call and stands.
    REQUIRE(body["outputs"][2]["transcode"]["bitrate_kbps"] == 3000);
    REQUIRE_FALSE(body["outputs"][0].contains("transcode"));
  }

  SECTION("a budget above the source rate is capped at the source")
  {
    REQUIRE(control_client::prepare_hosted_body(
        portal, codec::h264, out, err, 30, 20000, 1920, 1080, 8000, 12000));
    REQUIRE(json::parse(out)["outputs"][1]["transcode"]["bitrate_kbps"]
            == 8000);
  }

  SECTION("no measurement falls back to the sending rate, below the ceiling")
  {
    REQUIRE(control_client::prepare_hosted_body(
        portal, codec::h264, out, err, 30, 20000, 1920, 1080, 8000, 0));
    REQUIRE(json::parse(out)["outputs"][1]["transcode"]["bitrate_kbps"]
            == 8000);
  }

  SECTION("a target never reaches the transport ceiling")
  {
    // Even a measurement at the ceiling cannot lift the target there: the last
    // tenth is the recovery ring's.
    REQUIRE(control_client::prepare_hosted_body(
        portal, codec::h264, out, err, 30, 20000, 1920, 1080, 19000, 20000));
    const json body = json::parse(out);
    REQUIRE(body["outputs"][1]["transcode"]["bitrate_kbps"] == 18000);
    REQUIRE(body["outputs"][1]["transcode"]["bitrate_kbps"].get<int>() < 20000);
  }

  SECTION("a trickle of a measurement is floored at a usable tier")
  {
    REQUIRE(control_client::prepare_hosted_body(
        portal, codec::h264, out, err, 30, 20000, 1920, 1080, 8000, 180));
    REQUIRE(json::parse(out)["outputs"][1]["transcode"]["bitrate_kbps"] == 500);
  }
}

TEST_CASE("an opted-in transcode with no target is given the encoder's cap",
          "[hosted][control]")
{
  // A transcode block the portal could not derive carries no bitrate; the
  // encoder's cap is the only figure that exists. Without a source rate or a
  // ceiling nothing is written, so the receiver's own default stands.
  const char* portal = R"({
    "schema_version": 2,
    "session_id": "s_9f2c",
    "outputs": [
      {"id": "out1", "type": "rtmp", "url": "rtmp://a.example/live2",
       "transcode": {"codec": "h265", "gop": 120}}
    ]
  })";
  std::string out;
  std::string err;
  REQUIRE(control_client::prepare_hosted_body(
      portal, codec::h265, out, err, 30, 20000, 1920, 1080, 7000));
  REQUIRE(json::parse(out)["outputs"][0]["transcode"]["bitrate_kbps"] == 7000);

  // Nothing known about either input: the key is not invented.
  REQUIRE(control_client::prepare_hosted_body(
      portal, codec::h265, out, err, 30, 0, 1920, 1080, 0));
  REQUIRE_FALSE(
      json::parse(out)["outputs"][0]["transcode"].contains("bitrate_kbps"));
}

TEST_CASE("a measured rate is snapped to a standard broadcast rate",
          "[encode][fps]")
{
  // Two seconds of frames has to land on a whole number, so a measured rate is
  // snapped to the standard rate the source is running at rather than used as
  // measured. This is the rule the gop above depends on.
  SECTION("standard rates pass through unchanged")
  {
    REQUIRE(fps_snap::standard_rate(24, 1) == 24);
    REQUIRE(fps_snap::standard_rate(25, 1) == 25);
    REQUIRE(fps_snap::standard_rate(30, 1) == 30);
    REQUIRE(fps_snap::standard_rate(50, 1) == 50);
    REQUIRE(fps_snap::standard_rate(60, 1) == 60);
  }

  SECTION("the NTSC drop rates snap up to their nominal rate")
  {
    REQUIRE(fps_snap::standard_rate(30000, 1001)
            == 30);  // 29.97 -> 30, gop 60 not 59
    REQUIRE(fps_snap::standard_rate(60000, 1001) == 60);  // 59.94 -> 60
    REQUIRE(fps_snap::standard_rate(24000, 1001) == 24);  // 23.976 -> 24
  }

  SECTION("a drifting or stalled source cannot produce an odd gop")
  {
    // A measured 30.17 must give 30 (gop 60), and a stalled source reading 29.0
    // must not give 58.
    REQUIRE(fps_snap::standard_rate(3017, 100) == 30);
    REQUIRE(fps_snap::standard_rate(29, 1) == 30);
    REQUIRE(fps_snap::standard_rate(511, 10) == 50);  // 51.1 -> 50
  }

  SECTION("no measurement means no answer")
  {
    REQUIRE(fps_snap::standard_rate(0, 1) == 0);
    REQUIRE(fps_snap::standard_rate(30, 0) == 0);
  }
}

TEST_CASE("an unusable allocation body is refused rather than sent",
          "[hosted][control]")
{
  std::string out;
  std::string err;
  REQUIRE_FALSE(control_client::prepare_hosted_body(
      "not json at all", codec::h264, out, err));
  REQUIRE_FALSE(err.empty());

  // Valid JSON that is not an object is equally unusable: there is no document
  // to correct, and posting it would earn a confusing 400 from the receiver.
  err.clear();
  REQUIRE_FALSE(
      control_client::prepare_hosted_body("[1,2]", codec::h264, out, err));
  REQUIRE_FALSE(err.empty());

  err.clear();
  REQUIRE_FALSE(control_client::prepare_hosted_body("", codec::h264, out, err));
  REQUIRE_FALSE(err.empty());
}

// ---------------------------------------------------------------------------
// E4: the held-session poll decision.
//
// Pure, so the branch that matters most -- re-post ONLY when the receiver
// answered and said it is idle -- is pinned without a receiver or a clock.
// ---------------------------------------------------------------------------
TEST_CASE("the held-session poll re-posts only into a receiver that answered",
          "[hosted][e4]")
{
  // A receiver that says it is encoding: nothing to do, still poll at the
  // normal rate.
  {
    const hold_poll_decision d = decide_hold_poll(true, true, 0);
    CHECK_FALSE(d.repost_start);
    CHECK(d.next_poll_seconds == 5);
  }

  // The node rebooted or its container was OOM-killed: idle, and the stored
  // body has to go back.
  {
    const hold_poll_decision d = decide_hold_poll(true, false, 0);
    CHECK(d.repost_start);
    CHECK(d.next_poll_seconds == 5);
  }

  // The receiver did not answer. Re-posting into an unknown state is how a
  // poll becomes a retry storm against a node that is still booting.
  {
    const hold_poll_decision d = decide_hold_poll(false, false, 1);
    CHECK_FALSE(d.repost_start);
  }
}

TEST_CASE("repeated status failures lengthen the poll interval", "[hosted][e4]")
{
  // 5, 10, 20, 40, then the 60 s ceiling -- and it stays there.
  CHECK(decide_hold_poll(false, false, 0).next_poll_seconds == 5);
  CHECK(decide_hold_poll(false, false, 1).next_poll_seconds == 10);
  CHECK(decide_hold_poll(false, false, 2).next_poll_seconds == 20);
  CHECK(decide_hold_poll(false, false, 3).next_poll_seconds == 40);
  CHECK(decide_hold_poll(false, false, 4).next_poll_seconds == 60);
  CHECK(decide_hold_poll(false, false, 100).next_poll_seconds == 60);

  // A success resets the interval: failures are consecutive, not cumulative.
  CHECK(decide_hold_poll(true, true, 0).next_poll_seconds == 5);
}
