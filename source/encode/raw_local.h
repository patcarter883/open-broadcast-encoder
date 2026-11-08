// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include <gst/gst.h>

#include "encode/raw_local_port.h"

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

// Asks whether the announced wire format is acceptable given the selected codec
// and destinations: an empty return accepts the stream, anything else is the
// reason to refuse it.
using format_verdict_fn = std::function<std::string(std::uint32_t)>;

// How the reader treats a push the appsrc will not take.
//
//   backpressure -- the queue is full because its consumer is behind, and that
//                   consumer is the encoder: end the stream rather than invent
//                   a lossless one. OBS reconnects and the encoder catches up.
//   drop         -- the queue is full because a preview sink stalled, and the
//                   socket MATTERS MORE than the frame: count it, keep reading.
//                   Ending the stream here is what made OBS report "peer
//                   unreachable for 90 consecutive frames".
enum class push_mode
{
  backpressure,
  drop
};

class raw_local_input
{
public:
  // `format_verdict` carries the encoder's policy (see format_verdict_fn): the
  // reader itself never decides what a format is good enough for.
  raw_local_input(GstElement* video_src,
                  GstElement* audio_src,
                  std::uint16_t port,
                  std::function<void(const std::string&)> log_func,
                  format_verdict_fn format_verdict = {},
                  std::function<void(const std::string&)> status_func = {},
                  push_mode mode = push_mode::backpressure);
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
  // A reader-state line for the operator, separate from the log: it says what
  // the ingest is doing NOW (listening where, whether OBS is attached, frames
  // arriving), which is what tells a dead input from a healthy one.
  void status(const std::string& msg) const;
  auto open_listener() -> bool;
  auto accept_connection() -> int;
  auto read_exact(int fd, void* buf, std::size_t len) -> bool;
  auto read_stream_header(int fd) -> bool;
  void frame_loop(int fd);
  auto read_and_push(int fd) -> bool;
  auto read_planar_as_interleaved(int fd,
                                  std::uint8_t* dst,
                                  std::uint32_t payload_bytes) -> bool;
  // Preview pushes: never block, drop instead, and count what was dropped.
  void push_preview(GstElement* src,
                    GstBuffer* buf,
                    std::uint32_t payload_bytes);
  void note_preview_drop();
  auto running_pts(std::uint64_t timestamp) -> std::uint64_t;
  void log(const std::string& msg) const;

  GstElement* video_src;
  GstElement* audio_src;
  std::uint16_t port;
  std::function<void(const std::string&)> log_func;
  std::function<std::string(std::uint32_t)> format_verdict_slot;
  std::function<void(const std::string&)> status_func;
  push_mode mode {push_mode::backpressure};

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
  // Frames a preview sink refused; always 0 in backpressure mode, which ends
  // the stream instead of skipping.
  std::uint64_t dropped_frames {0};
  // Drops since the last line was logged, and when that was: a stalled preview
  // drops every frame, so the report is rate-limited to one line a second
  // instead of one per frame.
  std::uint64_t dropped_since_log {0};
  std::chrono::steady_clock::time_point last_drop_log {};
  // Per-stream last PTS, so a timestamp at or behind the base can never
  // collapse to the same value twice (mpegtsmux drops non-advancing DTS).
  std::uint64_t last_video_pts {0};
  std::uint64_t last_audio_pts {0};
  bool video_pts_set {false};
  bool audio_pts_set {false};
};
