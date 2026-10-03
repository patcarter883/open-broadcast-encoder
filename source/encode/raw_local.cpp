// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <format>
#include <vector>

#include "encode/raw_local.h"

#include <arpa/inet.h>
#include <gst/app/gstappsrc.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
// ---- The wire, mirrored from obs-raw-output/src/raw-output.c ----------------
// Keep this in lockstep with that file: it is the authoritative definition of
// an external format, so a change there must change this too.
constexpr std::uint32_t kMagic = 0x4F424352u;  // 'OBCR'
constexpr std::uint32_t kVersion = 2u;

constexpr std::uint32_t kKindVideo = 0u;
constexpr std::uint32_t kKindAudio = 1u;
constexpr std::uint32_t kAudioF32Planar = 1u;

constexpr std::size_t kMaxPlanes = 3;

#pragma pack(push, 1)
struct obc_stream_header
{
  std::uint32_t magic;
  std::uint32_t version;
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t fps_num;
  std::uint32_t fps_den;
  std::uint32_t format;  // OBS enum video_format
  std::uint32_t planes;
  std::uint32_t linesize[kMaxPlanes];  // always 0: unknown until frame one
  std::uint32_t plane_bytes[kMaxPlanes];
  std::uint32_t reserved;
  std::uint32_t audio_rate;  // 0 => the source has no audio
  std::uint32_t audio_channels;
  std::uint32_t audio_format;
  std::uint32_t audio_reserved[5];
};

struct obc_frame_header
{
  std::uint64_t timestamp;
  std::uint32_t kind;
  std::uint32_t payload_bytes;
  std::uint32_t units;  // video: plane count; audio: sample frames
  std::uint32_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(obc_stream_header) == 92,
              "stream header must be exactly 92 bytes");
static_assert(sizeof(obc_frame_header) == 24,
              "frame header must be exactly 24 bytes");

// OBS's enum video_format in declaration order, mapped to GStreamer formats.
auto gst_video_format(std::uint32_t format) -> const char*
{
  switch (format) {
    case 1:
      return "I420";
    case 2:
      return "NV12";
    case 3:
      return "YVYU";
    case 4:
      return "YUY2";
    case 5:
      return "UYVY";
    case 6:
      return "RGBA";
    case 7:
      return "BGRA";
    case 8:
      return "BGRx";
    case 9:
      return "GRAY8";
    default:
      return nullptr;
  }
}

// Polling rather than a blocking accept/recv is what lets stop() return
// promptly: the thread re-checks its stop flag every kPollTimeoutMs instead of
// parking in a syscall while another thread tries to close the descriptor out
// from under it.
constexpr int kPollTimeoutMs = 200;

// The plugin sends 3.1 MB frames at 60 fps. A deep receive buffer keeps a
// scheduling hiccup on this side from becoming a dropped frame on that side.
constexpr int kReceiveBufferBytes = 8 * 1024 * 1024;
}  // namespace

raw_local_input::raw_local_input(
    GstElement* video_src,
    GstElement* audio_src,
    std::uint16_t port,
    std::function<void(const std::string&)> log_func)
    : video_src {video_src}
    , audio_src {audio_src}
    , port {port}
    , log_func {std::move(log_func)}
{
}

raw_local_input::~raw_local_input()
{
  this->stop();
}

void raw_local_input::start()
{
  if (this->reader.joinable()) {
    return;
  }
  this->stop_requested.store(false);
  this->reader = std::thread([this] { this->run(); });
}

void raw_local_input::stop()
{
  this->stop_requested.store(true);
  if (this->reader.joinable()) {
    this->reader.join();
  }
}

void raw_local_input::log(const std::string& msg) const
{
  if (this->log_func) {
    this->log_func(msg);
  }
}

auto raw_local_input::open_listener() -> bool
{
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    log(std::format("[raw] socket() failed: {}\n", std::strerror(errno)));
    return false;
  }

  int on = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  int rcvbuf = kReceiveBufferBytes;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(this->port);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    log(std::format("[raw] bind to port {} failed: {}\n",
                    this->port,
                    std::strerror(errno)));
    ::close(fd);
    return false;
  }
  if (::listen(fd, 1) != 0) {
    log(std::format("[raw] listen failed: {}\n", std::strerror(errno)));
    ::close(fd);
    return false;
  }

  this->listen_fd.store(fd);
  log(
      std::format("[raw] listening on port {} for the OBS raw output "
                  "(set OBC_RAW_TARGET=127.0.0.1:{})\n",
                  this->port,
                  this->port));
  return true;
}

auto raw_local_input::accept_connection() -> int
{
  const int fd = this->listen_fd.load();
  if (fd < 0) {
    return -1;
  }

  pollfd pfd {};
  pfd.fd = fd;
  pfd.events = POLLIN;
  if (::poll(&pfd, 1, kPollTimeoutMs) <= 0) {
    return -1;  // timeout, or interrupted: let the caller re-check the flag
  }
  return ::accept(fd, nullptr, nullptr);
}

auto raw_local_input::read_exact(int fd, void* buf, std::size_t len) -> bool
{
  auto* out = static_cast<std::uint8_t*>(buf);
  std::size_t got = 0;
  while (got < len) {
    if (this->stop_requested.load()) {
      return false;
    }

    pollfd pfd {};
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int ready = ::poll(&pfd, 1, kPollTimeoutMs);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (ready == 0) {
      continue;
    }

    const ssize_t n = ::recv(fd, out + got, len - got, 0);
    if (n == 0) {
      return false;  // peer closed
    }
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    got += static_cast<std::size_t>(n);
  }
  return true;
}

auto raw_local_input::read_stream_header(int fd) -> bool
{
  obc_stream_header hdr {};
  if (!this->read_exact(fd, &hdr, sizeof(hdr))) {
    log("[raw] no stream header (closed before any data arrived)\n");
    return false;
  }
  if (hdr.magic != kMagic) {
    log(std::format("[raw] bad magic 0x{:08X}: not an OBS raw output\n",
                    hdr.magic));
    return false;
  }
  if (hdr.version != kVersion) {
    log(
        std::format("[raw] wire version {} but this build speaks {}; rebuild "
                    "the plugin or update raw_local_input\n",
                    hdr.version,
                    kVersion));
    return false;
  }

  const char* const fmt = gst_video_format(hdr.format);
  if (fmt == nullptr) {
    log(std::format("[raw] unsupported OBS video_format {}: cannot set caps\n",
                    hdr.format));
    return false;
  }
  if (hdr.fps_num == 0 || hdr.fps_den == 0) {
    log("[raw] stream header has a zero frame rate\n");
    return false;
  }

  if (this->base_set
      && (hdr.width != this->width || hdr.height != this->height))
  {
    // A mid-session geometry change is legal to announce but most encoders
    // cannot renegotiate in place, so say so rather than debug it later.
    log(
        std::format("[raw] warning: geometry changed {}x{} -> {}x{}; the "
                    "encoder may not follow a mid-stream change\n",
                    this->width,
                    this->height,
                    hdr.width,
                    hdr.height));
  }

  this->width = hdr.width;
  this->height = hdr.height;
  this->fps_num = hdr.fps_num;
  this->fps_den = hdr.fps_den;

  GstCaps* vcaps = gst_caps_new_simple("video/x-raw",
                                       "format",
                                       G_TYPE_STRING,
                                       fmt,
                                       "width",
                                       G_TYPE_INT,
                                       static_cast<int>(hdr.width),
                                       "height",
                                       G_TYPE_INT,
                                       static_cast<int>(hdr.height),
                                       "framerate",
                                       GST_TYPE_FRACTION,
                                       static_cast<int>(hdr.fps_num),
                                       static_cast<int>(hdr.fps_den),
                                       nullptr);
  gst_app_src_set_caps(GST_APP_SRC(this->video_src), vcaps);
  gst_caps_unref(vcaps);

  this->audio_present = hdr.audio_rate > 0 && hdr.audio_channels > 0
      && hdr.audio_format == kAudioF32Planar;
  if (this->audio_present) {
    this->audio_rate = hdr.audio_rate;
    this->audio_channels = hdr.audio_channels;
    // Interleaved, not the channel-planar layout the wire uses: audioconvert
    // needs a GstAudioMeta to consume a non-interleaved appsrc buffer and asserts
    // in gst_audio_buffer_map without one. read_planar_as_interleaved() repacks
    // each chunk before it is pushed.
    GstCaps* acaps = gst_caps_new_simple("audio/x-raw",
                                         "format",
                                         G_TYPE_STRING,
                                         "F32LE",
                                         "layout",
                                         G_TYPE_STRING,
                                         "interleaved",
                                         "rate",
                                         G_TYPE_INT,
                                         static_cast<int>(hdr.audio_rate),
                                         "channels",
                                         G_TYPE_INT,
                                         static_cast<int>(hdr.audio_channels),
                                         nullptr);
    gst_app_src_set_caps(GST_APP_SRC(this->audio_src), acaps);
    gst_caps_unref(acaps);
  } else if (hdr.audio_rate > 0) {
    log(
        std::format("[raw] audio announced in an unknown format {}; sending "
                    "video only\n",
                    hdr.audio_format));
  }

  const std::string audio_desc = this->audio_present
      ? std::format("{} Hz {} ch float32 planar",
                    this->audio_rate,
                    this->audio_channels)
      : std::string("none");

  log(
      std::format("[raw] OBS stream: {}x{} @ {}/{} {} ({} plane(s)); audio "
                  "{}\n",
                  hdr.width,
                  hdr.height,
                  hdr.fps_num,
                  hdr.fps_den,
                  fmt,
                  hdr.planes,
                  audio_desc));
  return true;
}

void raw_local_input::frame_loop(int fd)
{
  while (!this->stop_requested.load()) {
    if (!this->read_and_push(fd)) {
      break;
    }
  }
}

auto raw_local_input::read_and_push(int fd) -> bool
{
  obc_frame_header fh {};
  if (!this->read_exact(fd, &fh, sizeof(fh))) {
    return false;
  }
  if (fh.payload_bytes == 0) {
    log("[raw] frame header carries an empty payload; dropping the stream\n");
    return false;
  }

  const bool is_video = fh.kind == kKindVideo;
  const bool is_audio = fh.kind == kKindAudio;
  if (!is_video && !is_audio) {
    log(std::format("[raw] unknown frame kind {}; dropping the stream\n",
                    fh.kind));
    return false;
  }

  if (is_audio && !this->audio_present) {
    // Audio we were never told how to interpret. Consume the payload anyway so
    // the reader stays aligned with the byte stream.
    std::vector<std::uint8_t> scratch(fh.payload_bytes);
    return this->read_exact(fd, scratch.data(), scratch.size());
  }

  GstBuffer* const buf =
      gst_buffer_new_allocate(nullptr, fh.payload_bytes, nullptr);
  if (buf == nullptr) {
    log("[raw] failed to allocate a frame buffer\n");
    return false;
  }

  GstMapInfo map {};
  if (!gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
    gst_buffer_unref(buf);
    log("[raw] failed to map a frame buffer for writing\n");
    return false;
  }
  const bool read_ok =
      is_video
          ? this->read_exact(fd, map.data, fh.payload_bytes)
          : this->read_planar_as_interleaved(fd, map.data, fh.payload_bytes);
  gst_buffer_unmap(buf, &map);
  if (!read_ok) {
    gst_buffer_unref(buf);
    return false;
  }

  // Both streams are measured from the one base, so the offset between them
  // survives: this is the sync, and nothing here may re-derive it per stream.
  GST_BUFFER_PTS(buf) = this->running_pts(fh.timestamp);
  if (is_video) {
    GST_BUFFER_DURATION(buf) =
        gst_util_uint64_scale(GST_SECOND * this->fps_den, 1, this->fps_num);
    this->video_frames++;
  } else {
    GST_BUFFER_DURATION(buf) =
        gst_util_uint64_scale(GST_SECOND, fh.units, this->audio_rate);
    this->audio_chunks++;
  }

  // push_buffer takes ownership of the buffer, including on failure.
  const GstFlowReturn ret = gst_app_src_push_buffer(
      GST_APP_SRC(is_video ? this->video_src : this->audio_src), buf);
  if (ret != GST_FLOW_OK) {
    log(std::format("[raw] appsrc push failed: {}\n", gst_flow_get_name(ret)));
    return false;
  }

  const std::uint64_t total = this->video_frames + this->audio_chunks;
  if (total % 600 == 0) {
    log(std::format("[raw] {} video frames, {} audio chunks ingesting\n",
                    this->video_frames,
                    this->audio_chunks));
  }
  return true;
}

// Wire audio is channel-planar: channel 0's samples, then channel 1's, and so
// on. GStreamer's audioconvert will not take that from an appsrc -- a plain
// buffer carries no GstAudioMeta, so gst_audio_buffer_map asserts and the branch
// dies with "The stream is in the wrong format" -- so repack to interleaved.
auto raw_local_input::read_planar_as_interleaved(int fd,
                                                 std::uint8_t* dst,
                                                 std::uint32_t payload_bytes)
    -> bool
{
  const std::size_t channels = this->audio_channels;
  if (channels == 0 || (payload_bytes % (channels * 4u)) != 0) {
    log(std::format("[raw] audio payload of {} bytes does not split into {} "
                    "channel(s); dropping the stream\n",
                    payload_bytes,
                    channels));
    return false;
  }

  std::vector<std::uint8_t> planar(payload_bytes);
  if (!this->read_exact(fd, planar.data(), planar.size())) {
    return false;
  }

  const std::size_t frames = payload_bytes / (channels * 4u);
  const auto* in = reinterpret_cast<const float*>(planar.data());
  auto* out = reinterpret_cast<float*>(dst);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (std::size_t channel = 0; channel < channels; ++channel) {
      out[(frame * channels) + channel] = in[(channel * frames) + frame];
    }
  }
  return true;
}

auto raw_local_input::running_pts(std::uint64_t timestamp) -> std::uint64_t
{
  if (!this->base_set) {
    this->base_timestamp = timestamp;
    this->base_set = true;
  }
  // Audio can start marginally before the first video frame (a chunk's
  // timestamp is its first sample), so clamp rather than wrap.
  return (timestamp > this->base_timestamp) ? (timestamp - this->base_timestamp)
                                            : 0;
}

void raw_local_input::run()
{
  if (!this->open_listener()) {
    return;
  }

  while (!this->stop_requested.load()) {
    const int fd = this->accept_connection();
    if (fd < 0) {
      continue;
    }

    log("[raw] OBS raw output connected\n");
    if (this->read_stream_header(fd)) {
      this->frame_loop(fd);
    }
    ::close(fd);

    // No EOS: OBS's plugin retries every couple of seconds, so the pipeline
    // stays up and a restarted OBS resumes into the same session.
    log(
        std::format("[raw] OBS raw output disconnected after {} video frames "
                    "and {} audio chunks; waiting for it to reconnect\n",
                    this->video_frames,
                    this->audio_chunks));
  }

  const int fd = this->listen_fd.exchange(-1);
  if (fd >= 0) {
    ::close(fd);
  }
}
