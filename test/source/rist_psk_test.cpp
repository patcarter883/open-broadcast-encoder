// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// Task 4a acceptance: the direct leg's PSK, end to end on loopback.
//
// The sender is the PRODUCTION path (transport::setup_rist_sender with the
// session's key in output_config::psk); the listener is a real libRIST receiver
// through the same rist-cpp wrapper the shipped receiver uses. The matrix is
// the one WORKLIST-12 measured on librist 0.2.20:
//
//   keyed sender   -> keyed receiver    : the payload arrives, zero loss
//   keyed sender   -> unkeyed receiver  : nothing arrives (no handshake)
//   unkeyed sender -> unkeyed receiver  : the payload arrives (today's
//   behaviour)
//
// A handshake that delivers zero bytes is NOT a pass for the keyed/keyed case,
// so the assertion is on the delivered byte count, not on a connection state.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/lib.h"
#include "transport/transport.h"

namespace
{
// 300 packets of the standard 1316-byte RIST payload, the same shape as the
// WORKLIST-12 measurement: 300 * 1316 = 394800 bytes.
constexpr std::size_t kRistPayload = 1316;
constexpr int kPacketCount = 300;
constexpr std::size_t kPayloadBytes = kRistPayload * kPacketCount;

// Log lines from either side, kept so the test can prove the secret never
// reaches one.
struct log_sink
{
  mutable std::mutex mutex;
  std::vector<std::string> lines;
  void add(const char* msg)
  {
    if (msg == nullptr) {
      return;
    }
    std::lock_guard<std::mutex> guard(mutex);
    lines.emplace_back(msg);
  }
  [[nodiscard]] auto contains(const std::string& needle) const -> bool
  {
    std::lock_guard<std::mutex> guard(mutex);
    for (const std::string& line : lines) {
      if (line.find(needle) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
};

// libRIST's callback carries its own arg; the transport's log callback is
// called with a null arg (main's rist_log_cb ignores it), so the sender's sink
// is reached through this pointer instead.
log_sink* g_sender_sink = nullptr;

auto log_cb(void* arg, enum rist_log_level /*level*/, const char* msg) -> int
{
  if (arg != nullptr) {
    static_cast<log_sink*>(arg)->add(msg);
  } else if (g_sender_sink != nullptr) {
    g_sender_sink->add(msg);
  }
  return 1;
}

// A UDP port the kernel says is free: bind, read the assignment, close.
auto free_port() -> int
{
  const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    return 0;
  }
  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  int port = 0;
  if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
    socklen_t len = sizeof(addr);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
      port = ntohs(addr.sin_port);
    }
  }
  ::close(sock);
  return port;
}

struct case_result
{
  std::uint64_t bytes = 0;
  int packets = 0;
  bool sender_set_up = false;
};

// One loopback run. `sender_psk` empty means the clear leg; the receiver's key
// is independent so an unkeyed listener can be paired with a keyed sender.
auto run_case(const std::string& sender_psk,
              const std::string& receiver_psk,
              log_sink& sender_log,
              log_sink& receiver_log) -> case_result
{
  case_result result;
  const int port = free_port();
  REQUIRE(port != 0);

  std::atomic<std::uint64_t> received_bytes {0};
  std::atomic<int> received_packets {0};

  // ---- Receiver (the referee) --------------------------------------------
  RISTNetReceiver receiver;
  receiver.networkDataCallback =
      [&received_bytes, &received_packets](
          const uint8_t* /*buf*/,
          size_t size,
          std::shared_ptr<RISTNetReceiver::NetworkConnection>& /*conn*/,
          rist_peer* /*peer*/,
          uint16_t /*id*/) -> int
  {
    received_bytes.fetch_add(size, std::memory_order_relaxed);
    received_packets.fetch_add(1, std::memory_order_relaxed);
    return 0;
  };
  // A librist listener in Advanced profile runs the connection auth handler: an
  // unset validator rejects every peer ("validateConnectionCallback not
  // implemented"), which would make the harness look like an encryption
  // failure. Accept unconditionally, as the shipped receiver does.
  receiver.validateConnectionCallback = [](std::string /*ip*/,
                                           uint16_t /*port*/)
      -> std::shared_ptr<RISTNetReceiver::NetworkConnection>
  { return std::make_shared<RISTNetReceiver::NetworkConnection>(); };

  RISTNetReceiver::RISTNetReceiverSettings receiver_settings;
  receiver_settings.mProfile = RIST_PROFILE_ADVANCED;
  receiver_settings.mLogLevel = RIST_LOG_WARN;
  receiver_settings.mPSK = receiver_psk;
  receiver_settings.mLogSetting->log_cb = &log_cb;
  receiver_settings.mLogSetting->log_cb_arg = &receiver_log;
  std::vector<std::string> listen_urls {"rist://@127.0.0.1:"
                                        + std::to_string(port)};
  REQUIRE(receiver.initReceiver(listen_urls, receiver_settings));

  // ---- Sender (the code under test) --------------------------------------
  output_config out;
  out.host = "127.0.0.1";
  out.port = port;
  out.streams = 1;
  out.bandwidth = rist_bandwidth_ceiling_kbps;
  out.psk = sender_psk;
  out.psk_aes = sender_psk.empty() ? 0 : 128;

  g_sender_sink = &sender_log;
  {
    transport sender;
    sender.set_log_callback(&log_cb);
    result.sender_set_up = sender.setup_rist_sender(out);
    REQUIRE(result.sender_set_up);

    // Let the handshake settle: RIST recovers the first packets from its
    // retransmission buffer, so this is pacing, not a correctness crutch.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    std::vector<std::uint8_t> packet(kRistPayload, 0x5a);
    for (int i = 0; i < kPacketCount; ++i) {
      REQUIRE(sender.send_buffer(packet, 0));
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    // Read until the counter stops moving for a second, or 6 s in total.
    std::uint64_t last = 0;
    auto quiet_since = std::chrono::steady_clock::now();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      const std::uint64_t now = received_bytes.load();
      if (now != last) {
        last = now;
        quiet_since = std::chrono::steady_clock::now();
      } else if (std::chrono::steady_clock::now() - quiet_since
                 > std::chrono::milliseconds(1200))
      {
        break;
      }
    }
    result.bytes = received_bytes.load();
    result.packets = received_packets.load();
  }

  receiver.destroyReceiver();
  g_sender_sink = nullptr;
  // Printed unconditionally: the byte counts ARE the acceptance evidence, and a
  // passing assertion does not show them.
  std::fprintf(stderr,
               "rist_psk: sender=%s receiver=%s -> %llu bytes in %d packets\n",
               sender_psk.empty() ? "clear" : "keyed",
               receiver_psk.empty() ? "clear" : "keyed",
               static_cast<unsigned long long>(result.bytes),
               result.packets);
  return result;
}
}  // namespace

TEST_CASE("a keyed direct leg delivers its payload to a keyed receiver",
          "[psk]")
{
  const std::string secret = "obr-loopback-psk-4a";
  log_sink sender_log;
  log_sink receiver_log;
  const case_result r = run_case(secret, secret, sender_log, receiver_log);

  INFO("delivered " << r.bytes << " bytes in " << r.packets << " packets");
  // The whole payload, not just a handshake: 300 * 1316.
  REQUIRE(r.bytes == kPayloadBytes);
  // H2: the secret is never in a log line on either side.
  REQUIRE_FALSE(sender_log.contains(secret));
  REQUIRE_FALSE(receiver_log.contains(secret));
}

TEST_CASE("a keyed direct leg delivers nothing to an unkeyed receiver", "[psk]")
{
  const std::string secret = "obr-loopback-psk-4a";
  log_sink sender_log;
  log_sink receiver_log;
  const case_result r =
      run_case(secret, std::string {}, sender_log, receiver_log);

  INFO("delivered " << r.bytes << " bytes in " << r.packets << " packets");
  REQUIRE(r.bytes == 0);
  REQUIRE_FALSE(sender_log.contains(secret));
}

TEST_CASE("a clear direct leg still delivers its payload", "[psk]")
{
  log_sink sender_log;
  log_sink receiver_log;
  const case_result r =
      run_case(std::string {}, std::string {}, sender_log, receiver_log);

  INFO("delivered " << r.bytes << " bytes in " << r.packets << " packets");
  // The empty psk must keep today's behaviour exactly.
  REQUIRE(r.bytes == kPayloadBytes);
}

TEST_CASE("a session asking for AES-256 is refused, not downgraded", "[psk]")
{
  log_sink log;
  output_config out;
  out.host = "127.0.0.1";
  out.port = 24000;
  out.psk = "obr-loopback-psk-4a";
  out.psk_aes = 256;

  g_sender_sink = &log;
  transport sender;
  sender.set_log_callback(&log_cb);
  // This build keys at AES-128 only; a 128-bit key against a 256-bit listener
  // would carry no media at all, so the Start is refused instead.
  REQUIRE_FALSE(sender.setup_rist_sender(out));
  g_sender_sink = nullptr;
  REQUIRE(log.contains("PSK rejected"));
  REQUIRE_FALSE(log.contains(out.psk));
}

TEST_CASE("an over-long session key is refused", "[psk]")
{
  log_sink log;
  output_config out;
  out.host = "127.0.0.1";
  out.port = 24000;
  out.psk = std::string(129, 'k');  // one byte past librist's secret field
  out.psk_aes = 128;

  g_sender_sink = &log;
  transport sender;
  sender.set_log_callback(&log_cb);
  REQUIRE_FALSE(sender.setup_rist_sender(out));
  g_sender_sink = nullptr;
  REQUIRE(log.contains("PSK rejected"));
  REQUIRE_FALSE(log.contains(out.psk));
}

TEST_CASE("a session key is masked out of any log text", "[psk]")
{
  // The masking the sender relies on: once the key is known to the secrets
  // registry (which main and the backplane both do on arrival), the value is
  // replaced wherever it appears -- including in a line librist wrote itself.
  const std::string secret = "obr-mask-check-4a";
  secrets::register_secret(secret);
  const std::string line = "rist: using secret " + secret + " for peer 1";
  const std::string clean = secrets::redact(line);
  REQUIRE(clean.find(secret) == std::string::npos);
  REQUIRE(clean.find("***") != std::string::npos);
}
