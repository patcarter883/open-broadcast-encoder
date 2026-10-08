// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <cstdint>
#include <cstdlib>
#include <string>

#include "encode/capture_source.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
using socket_t = int;
#endif

namespace capture
{
namespace
{
#ifdef _WIN32
struct winsock_guard
{
  winsock_guard()
  {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
  }
  ~winsock_guard() { WSACleanup(); }
};
bool socket_valid(socket_t sock)
{
  return sock != INVALID_SOCKET;
}
void close_socket(socket_t sock)
{
  closesocket(sock);
}
#else
bool socket_valid(socket_t sock)
{
  return sock >= 0;
}
void close_socket(socket_t sock)
{
  ::close(sock);
}
#endif
}  // namespace

std::uint16_t parse_stream_port(const std::string& text, std::uint16_t fallback)
{
  if (text.empty()) {
    return fallback;
  }
  try {
    const int parsed = std::stoi(text);
    if (parsed < 1 || parsed > 65535) {
      return fallback;
    }
    return static_cast<std::uint16_t>(parsed);
  } catch (...) {
    return fallback;
  }
}

std::string local_address_for(const std::string& destination)
{
  if (destination.empty() || destination == "0.0.0.0") {
    return {};
  }

#ifdef _WIN32
  winsock_guard guard;
#endif

  const socket_t sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (!socket_valid(sock)) {
    return {};
  }

  sockaddr_in target {};
  target.sin_family = AF_INET;
  // A connect() on a datagram socket sets no traffic flowing; it only asks the
  // routing table which local address this destination would use. Port 9 is the
  // discard port and is never sent to.
  target.sin_port = htons(9);
  if (::inet_pton(AF_INET, destination.c_str(), &target.sin_addr) != 1) {
    close_socket(sock);
    return {};
  }

  if (::connect(sock, reinterpret_cast<sockaddr*>(&target), sizeof(target))
      != 0) {
    close_socket(sock);
    return {};
  }

  sockaddr_in local {};
  socklen_t len = sizeof(local);
  if (::getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len) != 0) {
    close_socket(sock);
    return {};
  }
  close_socket(sock);

  char buffer[INET_ADDRSTRLEN] = {};
  if (::inet_ntop(AF_INET, &local.sin_addr, buffer, sizeof(buffer)) == nullptr)
  {
    return {};
  }
  const std::string result {buffer};
  if (result.empty() || result == "0.0.0.0") {
    return {};
  }
  return result;
}

}  // namespace capture
