// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "discovery/discovery.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "discovery/mdns_parse.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socklen_t = int;
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

namespace discovery
{
namespace
{
constexpr const char* k_mdns_group = "224.0.0.251";
constexpr uint16_t k_mdns_port = 5353;

#ifdef _WIN32
struct wsa_guard
{
  bool ok = false;
  wsa_guard()
  {
    WSADATA d;
    ok = WSAStartup(MAKEWORD(2, 2), &d) == 0;
  }
  ~wsa_guard()
  {
    if (ok) {
      WSACleanup();
    }
  }
};
using sock_t = SOCKET;
constexpr sock_t k_bad_sock = INVALID_SOCKET;
void close_sock(sock_t s) { closesocket(s); }
#else
using sock_t = int;
constexpr sock_t k_bad_sock = -1;
void close_sock(sock_t s) { ::close(s); }
#endif

std::string source_ip(const sockaddr_in& from)
{
  char ip[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
  return ip;
}
}  // namespace

std::vector<bridge> discover_bridges(std::chrono::milliseconds timeout, const char* service)
{
  std::vector<bridge> found;
#ifdef _WIN32
  wsa_guard wsa;
  if (!wsa.ok) {
    return found;
  }
#endif

  sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == k_bad_sock) {
    return found;
  }

  // Ephemeral bind; QU responses come back to this source port unicast, so we
  // don't need to join the multicast group or contend for port 5353.
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = 0;
  if (bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
    close_sock(s);
    return found;
  }

  const uint8_t mttl = 1;  // link-local: never leave the subnet
  setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&mttl), sizeof(mttl));

  const std::vector<uint8_t> query = build_ptr_query(service);
  sockaddr_in dst{};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(k_mdns_port);
  inet_pton(AF_INET, k_mdns_group, &dst.sin_addr);
  sendto(s, reinterpret_cast<const char*>(query.data()), static_cast<int>(query.size()), 0,
         reinterpret_cast<sockaddr*>(&dst), sizeof(dst));

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  uint8_t buf[4096];
  while (std::chrono::steady_clock::now() < deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    timeval tv{};
    tv.tv_sec = static_cast<long>(remaining.count() / 1000);
    tv.tv_usec = static_cast<long>((remaining.count() % 1000) * 1000);
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    const int sel = select(static_cast<int>(s) + 1, &rfds, nullptr, nullptr, &tv);
    if (sel <= 0) {
      break;  // timeout or error
    }

    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    const int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                           reinterpret_cast<sockaddr*>(&from), &fromlen);
    if (n <= 0) {
      continue;
    }

    std::vector<bridge> parsed;
    if (!parse_response(buf, static_cast<std::size_t>(n), service, parsed)) {
      continue;
    }
    for (auto& b : parsed) {
      if (b.host.empty()) {
        b.host = source_ip(from);  // no A record — use the responder's address
      }
      // Dedup on host:port across multiple responders/records.
      const bool dup = std::any_of(found.begin(), found.end(), [&](const bridge& e) {
        return e.host == b.host && e.port == b.port;
      });
      if (!dup && !b.host.empty() && b.port > 0) {
        found.push_back(std::move(b));
      }
    }
  }

  close_sock(s);
  return found;
}
}  // namespace discovery
