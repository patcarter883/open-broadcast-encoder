// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "encode/capture_input.h"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include "encode/capture_source.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
using socket_t = int;
#endif

namespace
{
// A datagram is at most ~64 KiB; the node sends 1316-byte TS-aligned packets.
constexpr std::size_t k_receive_buffer = 65535;
// Recv timeout, so stop() is responsive rather than waiting on a datagram that
// may never come.
constexpr int k_recv_timeout_ms = 500;
// How often the reader reports what it is receiving. This is the evidence that
// the link is up and how fast it is, visible in the encode log without a
// sniffer.
constexpr int k_report_interval_ms = 5000;

#ifdef _WIN32
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

capture_input::capture_input(GstElement* ts_src,
                             std::string source_address,
                             std::uint16_t port,
                             std::function<void(const std::string&)> log_func)
    : ts_src {ts_src}
    , source_address {std::move(source_address)}
    , port {port}
    , log_func {std::move(log_func)}
{
}

capture_input::~capture_input()
{
  this->stop();
}

void capture_input::start()
{
  if (this->reader.joinable()) {
    return;
  }
  this->stop_requested.store(false, std::memory_order_relaxed);
  this->reader = std::thread([this] { this->run(); });
}

void capture_input::stop()
{
  this->stop_requested.store(true, std::memory_order_relaxed);
  if (this->reader.joinable()) {
    this->reader.join();
  }
}

auto capture_input::open_socket() -> int
{
  const socket_t sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (!socket_valid(sock)) {
    return -1;
  }

  const int yes = 1;
  // SO_REUSEADDR only. SO_REUSEPORT on a unicast socket would hand datagrams to
  // a single member of the reuseport group, so a second listener (or a stale
  // one) would silently starve this reader -- the same trap the bridge browser
  // carries, in the other direction.
  ::setsockopt(sock,
               SOL_SOCKET,
               SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes),
               sizeof(yes));

  timeval timeout {};
  timeout.tv_sec = k_recv_timeout_ms / 1000;
  timeout.tv_usec = (k_recv_timeout_ms % 1000) * 1000;
  ::setsockopt(sock,
               SOL_SOCKET,
               SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));

  // Prefer the interface that routes to the camera (the ingest point the node
  // sends TO). If that cannot be resolved, or the specific bind is refused
  // (another process holds that exact address), fall back to the wildcard so a
  // route hiccup is a fallback rather than a failure (DT-19).
  const std::string local_address =
      capture::local_address_for(this->source_address);
  const char* bind_label = "0.0.0.0";
  bool bound = false;

  if (!local_address.empty()) {
    sockaddr_in local {};
    local.sin_family = AF_INET;
    local.sin_port = htons(this->port);
    if (::inet_pton(AF_INET, local_address.c_str(), &local.sin_addr) == 1) {
      if (::bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0)
      {
        bound = true;
        bind_label = local_address.c_str();
      }
    }
  }

  if (!bound) {
    sockaddr_in wildcard {};
    wildcard.sin_family = AF_INET;
    wildcard.sin_addr.s_addr = htonl(INADDR_ANY);
    wildcard.sin_port = htons(this->port);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&wildcard), sizeof(wildcard))
        != 0) {
      this->log(std::format(
          "jpegxs_capture: could not bind {}:{} ({}); the camera link is not "
          "being received.\n",
          local_address.empty() ? "0.0.0.0" : local_address,
          this->port,
          std::strerror(errno)));
      close_socket(sock);
      return -1;
    }
    bind_label = "0.0.0.0";
  }

  this->log(std::format(
      "jpegxs_capture: listening for the JPEG XS camera link on {}:{} {}\n",
      bind_label,
      this->port,
      this->source_address.empty()
          ? "(any sender)"
          : std::format("(from {})", this->source_address)));
  return static_cast<int>(sock);
}

auto capture_input::from_selected_sender(const std::string& sender) const
    -> bool
{
  if (this->source_address.empty()) {
    return true;
  }
  return sender == this->source_address;
}

void capture_input::run()
{
  const int fd = this->open_socket();
  if (fd < 0) {
    return;
  }
  this->sock_fd.store(fd, std::memory_order_release);

  std::vector<std::uint8_t> buffer(k_receive_buffer);
  auto last_report = std::chrono::steady_clock::now();
  std::uint64_t window_datagrams = 0;
  std::uint64_t window_bytes = 0;

  while (!this->stop_requested.load(std::memory_order_relaxed)) {
    sockaddr_in sender {};
    socklen_t sender_len = sizeof(sender);
    const auto received = ::recvfrom(fd,
                                     reinterpret_cast<char*>(buffer.data()),
                                     static_cast<int>(k_receive_buffer),
                                     0,
                                     reinterpret_cast<sockaddr*>(&sender),
                                     &sender_len);
    if (received <= 0) {
      continue;  // the timeout elapsed, or a transient error; either is
                 // ordinary
    }

    const auto received_size = static_cast<std::size_t>(received);

    char sender_text[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &sender.sin_addr, sender_text, sizeof(sender_text));
    if (!this->from_selected_sender(std::string {sender_text})) {
      ++this->dropped_wrong_sender;
      continue;
    }

    // Push the datagram UNCHANGED. tsparse/tsdemux downstream do the parsing,
    // and svtjpegxsdec the decoding -- the SAME decode path the mpegts mode
    // uses. This class does not parse MPEG-TS.
    GstBuffer* gst_buffer =
        gst_buffer_new_allocate(nullptr, received_size, nullptr);
    if (gst_buffer == nullptr) {
      continue;
    }
    GstMapInfo map {};
    if (gst_buffer_map(gst_buffer, &map, GST_MAP_WRITE) == 0) {
      gst_buffer_unref(gst_buffer);
      continue;
    }
    std::memcpy(map.data, buffer.data(), received_size);
    gst_buffer_unmap(gst_buffer, &map);

    const GstFlowReturn flow =
        gst_app_src_push_buffer(GST_APP_SRC(this->ts_src), gst_buffer);
    if (flow != GST_FLOW_OK) {
      // The appsrc refused it (flushing or over max-bytes). The camera keeps
      // sending, so this is a dropped datagram, not a fatal condition.
      continue;
    }

    ++this->datagrams;
    this->bytes += received_size;
    ++window_datagrams;
    window_bytes += received_size;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_report)
            .count();
    if (elapsed_ms >= k_report_interval_ms) {
      const double seconds = static_cast<double>(elapsed_ms) / 1000.0;
      const double mbit =
          static_cast<double>(window_bytes) * 8.0 / seconds / 1'000'000.0;
      const double pps = static_cast<double>(window_datagrams) / seconds;
      this->log(std::format(
          "jpegxs_capture: {} datagrams/s, {:.1f} Mbit/s, {} total, {} dropped "
          "(wrong sender)\n",
          static_cast<std::uint64_t>(pps),
          mbit,
          this->datagrams,
          this->dropped_wrong_sender));
      last_report = now;
      window_datagrams = 0;
      window_bytes = 0;
    }
  }

  close_socket(fd);
  this->sock_fd.store(-1, std::memory_order_release);
  this->log(
      std::format("jpegxs_capture: reader stopped after {} datagrams "
                  "({} Mbit received).\n",
                  this->datagrams,
                  this->bytes * 8 / 1'000'000));
}

void capture_input::log(const std::string& msg) const
{
  if (this->log_func) {
    this->log_func(msg);
  }
}
