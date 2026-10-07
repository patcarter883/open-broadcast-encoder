// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <string>

#include "control/control.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

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
  REQUIRE(
      control_client::prepare_hosted_body(kPortalBody, codec::h265, out, err));
  REQUIRE(err.empty());

  const json body = json::parse(out);

  // The portal may lag the receiver's contract; the encoder must never ship a
  // stale version. The receiver accepts only an exact match, so this pins the
  // version that actually goes on the wire -- currently 4 (av1 target + output
  // scale). If the receiver's contract moves, this must move with
  // control_client::k_receiver_schema_version or every /start is refused.
  REQUIRE(body["schema_version"] == 4);
  // The portal can only guess the ingest codec. The encoder knows.
  REQUIRE(body["source"]["codec"] == "h265");
  REQUIRE(body["session_id"] == "s_9f2c");

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
