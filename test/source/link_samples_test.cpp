// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The link-sample wire contract, with no server: the body builder is pure and
// the transport is scripted (TELEMETRY_CONTRACT.md).

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "backplane/link_samples.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
// A scripted transport: records every call and answers with the queued statuses
// (200 once the queue runs out).
struct scripted_transport
{
  struct call
  {
    std::string method;
    std::string path;
    std::string token;
    nlohmann::json body;
  };

  std::vector<call> calls;
  std::vector<int> statuses;  // consumed in order
  bool throw_on_call = false;

  backplane::link_sample_reporter::transport_fn fn()
  {
    return [this](const std::string& method,
                  const std::string& path,
                  const std::string& token,
                  const std::string& body) -> std::pair<int, std::string>
    {
      if (throw_on_call) {
        throw std::runtime_error("socket closed");
      }
      calls.push_back(
          {method, path, token, nlohmann::json::parse(body, nullptr, false)});
      const int status = statuses.empty() ? 200 : statuses.front();
      if (!statuses.empty()) {
        statuses.erase(statuses.begin());
      }
      return {status, R"({"ok":true})"};
    };
  }
};

backplane::link_reading full_reading()
{
  backplane::link_reading reading;
  reading.quality = 96.8;
  reading.rtt_ms = 42;
  reading.bandwidth_bps = 19880000;
  reading.retry_bandwidth_bps = 41000;
  return reading;
}

peer_stats::aggregate one_leg()
{
  peer_stats::sample sample;
  sample.peer_id = 7;
  sample.cname = "wan";
  sample.quality = 97.5;
  sample.rtt_ms = 42;
  sample.bandwidth = 19880000;
  sample.retry_bandwidth = 41000;
  sample.sent = 1000;
  sample.retransmitted = 3;

  peer_stats::registry registry;
  registry.observe(sample, 1000);
  return registry.total();
}
}  // namespace

TEST_CASE("the sample carries the encoder envelope", "[link_samples]")
{
  const auto body = backplane::link_sample_body(
      "s_9f2c7a41", 409, "2026-11-13T13:05:02+11:00", full_reading());

  REQUIRE(body.at("session_public_id") == "s_9f2c7a41");
  REQUIRE(body.at("producer") == "encoder");
  REQUIRE(body.at("seq") == 409);
  REQUIRE(body.at("seq").is_number_unsigned());
  REQUIRE(body.at("sampled_at") == "2026-11-13T13:05:02+11:00");
}

TEST_CASE("a sender leaves every receiver-side field null", "[link_samples]")
{
  // The whole point of the contract: a sender cannot see the receiver's inbound
  // counters, so those fields are null. A 0 here would assert a perfect link.
  const auto body = backplane::link_sample_body(
      "s_1", 0, "2026-11-13T13:05:02+11:00", full_reading());

  for (const char* field : {"received_window",
                            "missing",
                            "recovered_window",
                            "recovered_one_retry_window",
                            "lost_window",
                            "reordered",
                            "received_total",
                            "recovered_total",
                            "recovered_one_retry_total",
                            "lost_total"})
  {
    CAPTURE(field);
    REQUIRE(body.contains(field));
    REQUIRE(body.at(field).is_null());
    REQUIRE_FALSE(body.at(field).is_number());
  }
}

TEST_CASE("an unobserved sender field is null, not zero", "[link_samples]")
{
  // Nothing observed at all: no peer has reported yet. Every link field,
  // including the four a sender CAN fill, is null rather than a zero.
  for (const char* field :
       {"quality", "rtt_ms", "bandwidth_bps", "retry_bandwidth_bps"})
  {
    CAPTURE(field);
    const auto body = backplane::link_sample_body(
        "s_1", 0, "2026-11-13T13:05:02+11:00", backplane::link_reading {});
    REQUIRE(body.at(field).is_null());
    REQUIRE_FALSE(body.at(field).is_number());
  }
}

TEST_CASE("the sender's own figures are numbers carried from the stats",
          "[link_samples]")
{
  const auto body = backplane::link_sample_body(
      "s_1", 0, "2026-11-13T13:05:02+11:00", full_reading());

  REQUIRE(body.at("quality").is_number());
  REQUIRE(body.at("quality").get<double>() == 96.8);
  REQUIRE(body.at("rtt_ms").is_number_integer());
  REQUIRE(body.at("rtt_ms") == 42);
  REQUIRE(body.at("bandwidth_bps").is_number_unsigned());
  REQUIRE(body.at("bandwidth_bps") == 19880000);
  REQUIRE(body.at("retry_bandwidth_bps").is_number_unsigned());
  REQUIRE(body.at("retry_bandwidth_bps") == 41000);
}

TEST_CASE("the reading is the bond, and no leg means no figures",
          "[link_samples]")
{
  const auto reading = backplane::link_reading_from(one_leg());
  REQUIRE(reading.quality.has_value());
  REQUIRE(reading.quality.value() == 97.5);
  REQUIRE(reading.rtt_ms.value() == 42);
  REQUIRE(reading.bandwidth_bps.value() == 19880000);
  REQUIRE(reading.retry_bandwidth_bps.value() == 41000);

  const auto empty = backplane::link_reading_from(peer_stats::aggregate {});
  REQUIRE_FALSE(empty.quality.has_value());
  REQUIRE_FALSE(empty.rtt_ms.has_value());
  REQUIRE_FALSE(empty.bandwidth_bps.has_value());
  REQUIRE_FALSE(empty.retry_bandwidth_bps.has_value());
}

TEST_CASE("a sample posts to the session with its control token",
          "[link_samples]")
{
  scripted_transport transport;
  backplane::link_sample_reporter reporter(transport.fn());

  const auto result =
      reporter.report("s_9f2c7a41", "control-secret", full_reading());

  REQUIRE(result.ok);
  REQUIRE(result.http_status == 200);
  REQUIRE(transport.calls.size() == 1);
  REQUIRE(transport.calls.front().method == "POST");
  REQUIRE(transport.calls.front().path
          == "/api/v1/sessions/s_9f2c7a41/link-samples");
  REQUIRE(transport.calls.front().token == "control-secret");
  REQUIRE(transport.calls.front().body.at("producer") == "encoder");
}

TEST_CASE("seq increments per emitted sample", "[link_samples]")
{
  scripted_transport transport;
  backplane::link_sample_reporter reporter(transport.fn());

  reporter.report("s_1", "tok", full_reading());
  reporter.report("s_1", "tok", full_reading());
  reporter.report("s_1", "tok", full_reading());

  REQUIRE(transport.calls.size() == 3);
  REQUIRE(transport.calls.at(0).body.at("seq") == 0);
  REQUIRE(transport.calls.at(1).body.at("seq") == 1);
  REQUIRE(transport.calls.at(2).body.at("seq") == 2);
  REQUIRE(reporter.next_seq("s_1") == 3);

  // The sequence is per session: a second session starts at 0 again.
  reporter.report("s_2", "tok", full_reading());
  REQUIRE(transport.calls.at(3).body.at("seq") == 0);
  REQUIRE(reporter.next_seq("s_1") == 3);
}

TEST_CASE("a refused sample does not consume its seq", "[link_samples]")
{
  scripted_transport transport;
  transport.statuses = {500};
  backplane::link_sample_reporter reporter(transport.fn());

  const auto refused = reporter.report("s_1", "tok", full_reading());
  REQUIRE_FALSE(refused.ok);
  REQUIRE(refused.http_status == 500);
  REQUIRE(reporter.next_seq("s_1") == 0);

  // The retry reuses the number: the backplane is idempotent on
  // (session, producer, seq), so a sample that did land is a no-op and one that
  // did not leaves no gap.
  const auto accepted = reporter.report("s_1", "tok", full_reading());
  REQUIRE(accepted.ok);
  REQUIRE(transport.calls.at(1).body.at("seq") == 0);
  REQUIRE(reporter.next_seq("s_1") == 1);
}

TEST_CASE("a throwing transport fails the sample and not the caller",
          "[link_samples]")
{
  scripted_transport transport;
  transport.throw_on_call = true;
  backplane::link_sample_reporter reporter(transport.fn());

  backplane::link_sample_result result;
  REQUIRE_NOTHROW(result = reporter.report("s_1", "tok", full_reading()));
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.http_status == 0);
  REQUIRE_FALSE(result.error.empty());
  REQUIRE(reporter.next_seq("s_1") == 0);
}

TEST_CASE("a session without a control token sends nothing", "[link_samples]")
{
  scripted_transport transport;
  backplane::link_sample_reporter reporter(transport.fn());

  const auto result = reporter.report("s_1", "", full_reading());
  REQUIRE_FALSE(result.ok);
  REQUIRE(transport.calls.empty());
  REQUIRE(reporter.next_seq("s_1") == 0);
}

TEST_CASE("no transport is a clean failure", "[link_samples]")
{
  backplane::link_sample_reporter reporter;
  const auto result = reporter.report("s_1", "tok", full_reading());
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("sampled_at is ISO-8601 with an offset", "[link_samples]")
{
  const auto stamp = backplane::iso8601_at(std::chrono::system_clock::now());
  // yyyy-mm-ddThh:mm:ss+hh:mm is 25 characters, and %z alone gives 24.
  REQUIRE(stamp.size() == 25);
  REQUIRE(stamp.at(10) == 'T');
  REQUIRE((stamp.at(19) == '+' || stamp.at(19) == '-'));
  REQUIRE(stamp.at(22) == ':');
}
