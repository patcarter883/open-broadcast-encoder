// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The preview queue, exercised against a real appsrc instead of described in a
// string. The reader's promise is that a stalled preview costs frames and never
// the ingest: it must return from every push promptly, keep the queue inside
// its cap, and COUNT what it dropped.
//
// The last point is why this test exists at all. With block=false and
// leaky-type=downstream the element drops the oldest frame itself and still
// returns GST_FLOW_OK, so a push-failure counter -- the reader's only signal
// before the level check -- stayed at zero while every frame was being
// discarded. Here the sink is genuinely stalled (the pipeline is left in
// PAUSED, so nothing consumes the queue) and the drops are visible.

#include <chrono>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include "encode/appsrc_policy.h"

namespace
{
// A small frame (720p BGRA) so 600 of them stay well inside the default
// max-bytes than the caps would size, and the run is a fraction of a second.
constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 720;

struct pipeline_guard
{
  GstElement* pipeline = nullptr;

  ~pipeline_guard()
  {
    if (pipeline != nullptr) {
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
    }
  }
  pipeline_guard(const pipeline_guard&) = delete;
  pipeline_guard& operator=(const pipeline_guard&) = delete;
  pipeline_guard() = default;
};
}  // namespace

TEST_CASE("a stalled preview sink drops frames without parking the reader",
          "[encode][preview][gst]")
{
  gst_init(nullptr, nullptr);

  const std::uint64_t frame_bytes =
      appsrc_policy::preview_frame_bytes("BGRA", kWidth, kHeight);
  const std::uint64_t cap =
      appsrc_policy::preview_video_max_bytes("BGRA", kWidth, kHeight);
  REQUIRE(cap > frame_bytes);

  pipeline_guard guard;
  GError* error = nullptr;
  const std::string description = appsrc_policy::preview_video_src()
      + " ! fakesink name=sink sync=false async=false";
  guard.pipeline = gst_parse_launch(description.c_str(), &error);
  REQUIRE(guard.pipeline != nullptr);
  REQUIRE(error == nullptr);

  GstElement* const src =
      gst_bin_get_by_name(GST_BIN(guard.pipeline), "rawvideo");
  REQUIRE(src != nullptr);
  g_object_set(src, "max-bytes", static_cast<guint64>(cap), nullptr);

  GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                      "format",
                                      G_TYPE_STRING,
                                      "BGRA",
                                      "width",
                                      G_TYPE_INT,
                                      static_cast<int>(kWidth),
                                      "height",
                                      G_TYPE_INT,
                                      static_cast<int>(kHeight),
                                      nullptr);
  gst_app_src_set_caps(GST_APP_SRC(src), caps);
  gst_caps_unref(caps);

  // PAUSED, never PLAYING: the sink takes no buffers, which is exactly what a
  // frozen preview window looks like to the element.
  gst_element_set_state(guard.pipeline, GST_STATE_PAUSED);

  std::uint64_t accepted = 0;
  std::uint64_t dropped = 0;
  std::uint64_t worst_level = 0;
  std::chrono::milliseconds worst_push {0};

  constexpr std::uint64_t kFrames = 600;
  for (std::uint64_t i = 0; i < kFrames; ++i) {
    GstBuffer* buf = gst_buffer_new_allocate(nullptr, frame_bytes, nullptr);
    REQUIRE(buf != nullptr);
    GST_BUFFER_PTS(buf) = i * GST_SECOND / 30;
    GST_BUFFER_DURATION(buf) = GST_SECOND / 30;

    guint64 level = 0;
    guint64 max_bytes = 0;
    g_object_get(
        src, "current-level-bytes", &level, "max-bytes", &max_bytes, nullptr);
    worst_level = level > worst_level ? level : worst_level;

    // The reader's rule, exactly: ask first, drop instead of pushing.
    if (!appsrc_policy::preview_accepts(level, frame_bytes, max_bytes)) {
      gst_buffer_unref(buf);
      ++dropped;
      continue;
    }

    const auto started = std::chrono::steady_clock::now();
    const GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(src), buf);
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    worst_push = took > worst_push ? took : worst_push;

    // The element reports success even when it discards: this is the signal
    // that made a push-failure counter useless.
    REQUIRE(ret == GST_FLOW_OK);
    ++accepted;
  }

  gst_object_unref(src);

  // The queue filled and the reader stopped pushing into it.
  CHECK(accepted > 0);
  CHECK(dropped > 0);
  CHECK(accepted + dropped == kFrames);
  // Nothing was ever queued beyond the cap.
  CHECK(worst_level <= cap);
  // And no push parked the reader: a stalled preview would otherwise hold this
  // loop for as long as the sink stayed frozen.
  INFO("worst push took " << worst_push.count() << " ms");
  CHECK(worst_push < std::chrono::milliseconds {100});
}
