// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include <gst/gst.h>

// Ingest for the OBS raw output plugin (open-broadcast/obs-raw-output).
//
// The plugin is the TCP *client*, so this side listens on `port`, accepts one
// connection at a time, and feeds two appsrcs in the encode pipeline:
// uncompressed video into `video_src`, synchronised audio into `audio_src`.
//
// Wire format v2 is defined authoritatively in obs-raw-output/src/raw-output.c:
// a 92-byte stream header, then 24-byte frame headers whose `kind` selects the
// stream, both multiplexed over that one connection.
//
// Sync. Every frame carries OBS's own timestamp, and OBS stamps video and audio
// from the *same* clock -- that shared clock is the A/V sync. This reader folds
// both streams onto a running timeline by subtracting ONE base (the first
// timestamp seen, from either stream), which preserves the offset between the
// two streams exactly while giving GStreamer a sane PTS origin. Re-stamping the
// streams independently is the thing that would actually break sync.
//
// Audio. The wire is channel-planar float32, but an appsrc buffer cannot be
// handed to audioconvert in that layout: with no GstAudioMeta describing the
// planes, audioconvert asserts in gst_audio_buffer_map and the branch dies with
// "The stream is in the wrong format". Each chunk is interleaved on the way in;
// the copy is a few kB per ~21 ms against ~187 MB/s of video.
class raw_local_input
{
public:
  static constexpr std::uint16_t default_port = 9300;

  raw_local_input(GstElement* video_src,
                  GstElement* audio_src,
                  std::uint16_t port,
                  std::function<void(const std::string&)> log_func);
  ~raw_local_input();

  raw_local_input(const raw_local_input&) = delete;
  raw_local_input& operator=(const raw_local_input&) = delete;
  raw_local_input(raw_local_input&&) = delete;
  raw_local_input& operator=(raw_local_input&&) = delete;

  // Spawns the reader thread and returns; the socket is opened on that thread.
  void start();
  // Asks the thread to finish and joins it. Safe to call more than once.
  void stop();

private:
  void run();
  auto open_listener() -> bool;
  auto accept_connection() -> int;
  auto read_exact(int fd, void* buf, std::size_t len) -> bool;
  auto read_stream_header(int fd) -> bool;
  void frame_loop(int fd);
  auto read_and_push(int fd) -> bool;
  auto read_planar_as_interleaved(int fd,
                                  std::uint8_t* dst,
                                  std::uint32_t payload_bytes) -> bool;
  auto running_pts(std::uint64_t timestamp) -> std::uint64_t;
  void log(const std::string& msg) const;

  GstElement* video_src;
  GstElement* audio_src;
  std::uint16_t port;
  std::function<void(const std::string&)> log_func;

  std::thread reader;
  std::atomic<bool> stop_requested {false};
  std::atomic<int> listen_fd {-1};

  // Shared PTS origin for both streams; see the class comment.
  std::uint64_t base_timestamp {0};
  bool base_set {false};

  // Stream properties, learnt from the header.
  std::uint32_t width {0};
  std::uint32_t height {0};
  std::uint32_t fps_num {0};
  std::uint32_t fps_den {0};
  bool audio_present {false};
  std::uint32_t audio_rate {0};
  std::uint32_t audio_channels {0};

  std::uint64_t video_frames {0};
  std::uint64_t audio_chunks {0};
};
