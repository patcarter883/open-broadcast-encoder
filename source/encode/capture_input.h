// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include <gst/gst.h>

// Ingest for a JPEG XS capture source on the LAN (MC4).
//
// The camera node (jpegxs-arm-port) muxes JPEG XS into MPEG-TS and sends it
// UNICAST to one ingest point. This reader is that ingest point: it binds a UDP
// socket and pushes each datagram, unchanged, into `ts_src` -- an appsrc capped
// `video/mpegts,systemstream=true`. From there the pipeline is the SAME one the
// mpegts input mode uses (tsparse -> tsdemux -> decodebin3 -> svtjpegxsdec), so
// there is exactly ONE JPEG XS decode path in the encoder, not a second one
// (MC4.2's rule).
//
// Why a reader and not `udpsrc`: a unicast datagram is delivered to one socket,
// and this must be the socket on the interface that routes to the camera (the
// ingest point the node is sending TO). udpsrc binds the wildcard, which both
// misses the point and collides with anything else holding a specific address
// on that port. Binding is the whole reason this class exists; the caps, the
// parsing and the decode are all reused.
//
// The port is the operator's (the plan's advert does not carry the unicast
// stream port -- see MC4's deviation note); `source_address` is the camera from
// the LAN browse and is used to pick the interface and to drop datagrams from
// any other sender. An empty source_address is the manual fallback (DT-19):
// bind the wildcard and accept any sender.

class capture_input
{
public:
  // `source_address` is the camera's address (from the browse); `port` is the
  // JPEG XS stream port to listen on. `ts_src` is the `capturets` appsrc in the
  // encode pipeline and is owned by the pipeline, not by this class.
  capture_input(GstElement* ts_src,
                std::string source_address,
                std::uint16_t port,
                std::function<void(const std::string&)> log_func);
  ~capture_input();

  capture_input(const capture_input&) = delete;
  capture_input& operator=(const capture_input&) = delete;
  capture_input(capture_input&&) = delete;
  capture_input& operator=(capture_input&&) = delete;

  // Spawns the reader thread and returns; the socket is opened on that thread.
  void start();
  // Asks the thread to finish and joins it. Safe to call more than once.
  void stop();

private:
  void run();
  // Create the UDP socket and bind it. Returns the fd, or -1 on failure. Binds
  // the interface that routes to the camera when one can be resolved, else the
  // wildcard -- and logs which, because an unexpected interface is the first
  // thing to check when no data arrives.
  auto open_socket() -> int;
  // True when a datagram's sender should be accepted: always, when no camera
  // address is selected, else only from that address.
  auto from_selected_sender(const std::string& sender) const -> bool;
  void log(const std::string& msg) const;

  GstElement* ts_src;
  std::string source_address;
  std::uint16_t port;
  std::function<void(const std::string&)> log_func;

  std::thread reader;
  std::atomic<bool> stop_requested {false};
  std::atomic<int> sock_fd {-1};

  std::uint64_t datagrams {0};
  std::uint64_t bytes {0};
  std::uint64_t dropped_wrong_sender {0};
};
