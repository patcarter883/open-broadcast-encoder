// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The AV1 muxing contract, checked end to end: an AV1 elementary stream muxed
// with the encoder's own mpegtsmux settings must come back out of tsdemux as
// video/x-av1. The PMT is inspected too, because that is what a non-GStreamer
// receiver has to read.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "encode/ts_mux.h"

#include <catch2/catch_test_macros.hpp>
#include <gst/gst.h>

namespace
{
constexpr size_t kTsPacketSize = 188;
constexpr int kProbeTimeoutSeconds = 60;

struct run_result
{
  bool ok = false;
  std::string error;
};

// Runs a gst-launch style description to EOS. after_launch is where pads that
// appear dynamically (tsdemux's) get their handlers connected, i.e. before the
// pipeline is set PLAYING.
run_result run_pipeline(
    const std::string& description,
    const std::function<void(GstElement*)>& after_launch = {})
{
  GError* parse_error = nullptr;
  GstElement* pipeline = gst_parse_launch(description.c_str(), &parse_error);
  if (parse_error != nullptr) {
    run_result result {false, parse_error->message};
    g_clear_error(&parse_error);
    if (pipeline != nullptr) {
      gst_object_unref(pipeline);
    }
    return result;
  }
  if (pipeline == nullptr) {
    return {false, "gst_parse_launch returned no pipeline"};
  }
  if (after_launch) {
    after_launch(pipeline);
  }

  const GstStateChangeReturn state =
      gst_element_set_state(pipeline, GST_STATE_PLAYING);
  run_result result {false, "state change failed"};
  GstBus* bus = gst_element_get_bus(pipeline);
  GstMessage* message = gst_bus_timed_pop_filtered(
      bus,
      static_cast<GstClockTime>(kProbeTimeoutSeconds) * GST_SECOND,
      static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
  if (message == nullptr) {
    result.error = "timed out";
  } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
    GError* error = nullptr;
    gchar* debug = nullptr;
    gst_message_parse_error(message, &error, &debug);
    result.error =
        (error != nullptr) ? error->message : "error without message";
    g_clear_error(&error);
    g_free(debug);
  } else {
    result.ok = state != GST_STATE_CHANGE_FAILURE;
  }
  if (message != nullptr) {
    gst_message_unref(message);
  }
  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  return result;
}

struct temp_dir
{
  std::filesystem::path path;

  temp_dir()
  {
    GError* error = nullptr;
    gchar* created = g_dir_make_tmp("obc-av1-ts-XXXXXX", &error);
    REQUIRE(created != nullptr);
    path = created;
    g_free(created);
    g_clear_error(&error);
  }
  ~temp_dir()
  {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  temp_dir(const temp_dir&) = delete;
  temp_dir& operator=(const temp_dir&) = delete;
};

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& file)
{
  std::ifstream stream(file, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

// First complete PSI section on pid, pointer_field consumed.
std::vector<std::uint8_t> first_section(const std::vector<std::uint8_t>& ts,
                                        int pid)
{
  std::vector<std::uint8_t> section;
  for (size_t offset = 0; offset + kTsPacketSize <= ts.size();
       offset += kTsPacketSize)
  {
    const std::uint8_t* packet = ts.data() + offset;
    if (packet[0] != 0x47) {
      continue;
    }
    if ((((packet[1] & 0x1f) << 8) | packet[2]) != pid) {
      continue;
    }
    const int adaptation = (packet[3] >> 4) & 0x03;
    if ((adaptation & 0x01) == 0) {
      continue;
    }
    size_t payload = 4;
    if ((adaptation & 0x02) != 0) {
      payload += 1 + packet[4];
    }
    if (payload >= kTsPacketSize) {
      continue;
    }
    if ((packet[1] & 0x40) != 0) {
      // pointer_field, then the bytes it points past.
      payload += 1 + packet[payload];
      if (payload >= kTsPacketSize) {
        continue;
      }
      section.assign(packet + payload, packet + kTsPacketSize);
    } else {
      section.insert(section.end(), packet + payload, packet + kTsPacketSize);
    }
    if (section.size() >= 3) {
      const size_t length = ((section[1] & 0x0f) << 8) | section[2];
      if (section.size() >= 3 + length) {
        section.resize(3 + length);
        return section;
      }
    }
  }
  return {};
}

int pmt_pid(const std::vector<std::uint8_t>& ts)
{
  const std::vector<std::uint8_t> pat = first_section(ts, 0x0000);
  if (pat.size() < 12 || pat[0] != 0x00) {
    return -1;
  }
  return ((pat[10] & 0x1f) << 8) | pat[11];
}

struct elementary_stream
{
  std::uint8_t stream_type = 0;
  int pid = 0;
  std::vector<std::uint8_t> descriptors;  // tag, length, payload ...
};

std::vector<elementary_stream> pmt_streams(const std::vector<std::uint8_t>& pmt)
{
  std::vector<elementary_stream> streams;
  if (pmt.size() < 16 || pmt[0] != 0x02) {
    return streams;
  }
  const size_t section_length = ((pmt[1] & 0x0f) << 8) | pmt[2];
  const size_t program_info_length = ((pmt[10] & 0x0f) << 8) | pmt[11];
  size_t cursor = 12 + program_info_length;
  const size_t end = 3 + section_length - 4;  // minus CRC32
  while (cursor + 5 <= end) {
    elementary_stream stream;
    stream.stream_type = pmt[cursor];
    stream.pid = ((pmt[cursor + 1] & 0x1f) << 8) | pmt[cursor + 2];
    const size_t info_length =
        ((pmt[cursor + 3] & 0x0f) << 8) | pmt[cursor + 4];
    cursor += 5;
    if (cursor + info_length > end) {
      break;
    }
    stream.descriptors.assign(
        pmt.begin() + static_cast<long>(cursor),
        pmt.begin() + static_cast<long>(cursor + info_length));
    streams.push_back(stream);
    cursor += info_length;
  }
  return streams;
}

// Payload of the first descriptor with the given tag, empty if absent.
std::vector<std::uint8_t> descriptor_payload(
    const std::vector<std::uint8_t>& descriptors, std::uint8_t tag)
{
  size_t cursor = 0;
  while (cursor + 2 <= descriptors.size()) {
    const std::uint8_t current = descriptors[cursor];
    const size_t length = descriptors[cursor + 1];
    if (cursor + 2 + length > descriptors.size()) {
      break;
    }
    if (current == tag) {
      return {descriptors.begin() + static_cast<long>(cursor + 2),
              descriptors.begin() + static_cast<long>(cursor + 2 + length)};
    }
    cursor += 2 + length;
  }
  return {};
}

struct pad_probe
{
  std::string caps;
  GstElement* sink = nullptr;
};

void on_pad_added(GstElement* /*demux*/, GstPad* pad, gpointer data)
{
  auto* probe = static_cast<pad_probe*>(data);
  if (probe->caps.empty()) {
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (caps == nullptr) {
      caps = gst_pad_query_caps(pad, nullptr);
    }
    if (caps != nullptr) {
      gchar* text = gst_caps_to_string(caps);
      probe->caps = text;
      g_free(text);
      gst_caps_unref(caps);
    }
  }
  GstPad* sink_pad = gst_element_get_static_pad(probe->sink, "sink");
  if (sink_pad != nullptr) {
    if (!gst_pad_is_linked(sink_pad)) {
      gst_pad_link(pad, sink_pad);
    }
    gst_object_unref(sink_pad);
  }
}

bool have_gst_elements()
{
  for (const char* name :
       {"mpegtsmux", "tsdemux", "tsparse", "av1enc", "av1parse"})
  {
    GstElementFactory* factory = gst_element_factory_find(name);
    if (factory == nullptr) {
      return false;
    }
    gst_object_unref(factory);
  }
  return true;
}

}  // namespace

TEST_CASE("AV1 muxed with the encoder settings parses back as video/x-av1")
{
  gst_init(nullptr, nullptr);
  if (!have_gst_elements()) {
    SKIP("GStreamer AV1/mpegts elements are not installed");
  }

  const temp_dir tmp;
  const std::filesystem::path ts_file = tmp.path / "av1.ts";
  const std::string mux = std::string("videotestsrc num-buffers=30 ! ")
      + "video/x-raw,width=320,height=240,framerate=25/1 ! "
      + "av1enc cpu-used=8 target-bitrate=500 ! av1parse ! "
      + std::string(ts_mux::av1_frame_caps) + " ! " + ts_mux::element(7)
      + " ! filesink location=" + ts_file.string();

  const run_result muxed = run_pipeline(mux);
  INFO("mux: " << muxed.error);
  REQUIRE(muxed.ok);
  REQUIRE(std::filesystem::file_size(ts_file) > 0);

  const std::vector<std::uint8_t> ts = read_bytes(ts_file);
  const int program_pid = pmt_pid(ts);
  REQUIRE(program_pid >= 0);
  const std::vector<elementary_stream> streams =
      pmt_streams(first_section(ts, program_pid));
  REQUIRE(streams.size() == 1);
  CHECK(streams[0].stream_type == ts_mux::av1_stream_type);
  CHECK(descriptor_payload(streams[0].descriptors, 0x05)
        == std::vector<std::uint8_t>(ts_mux::av1_registration.begin(),
                                     ts_mux::av1_registration.end()));
  // av1C (marker+version, then profile/level, then chroma, then reserved).
  const std::vector<std::uint8_t> av1c =
      descriptor_payload(streams[0].descriptors, 0x80);
  REQUIRE(av1c.size() == 4);
  CHECK(av1c[0] == 0x81);

  pad_probe probe;
  const std::string demux =
      "filesrc location=" + ts_file.string()
      + " ! tsparse set-timestamps=true alignment=7 ! tsdemux name=demux "
        "fakesink name=ts_sink sync=false";
  const run_result demuxed = run_pipeline(
      demux,
      [&probe](GstElement* pipeline)
      {
        GstElement* demux_element =
            gst_bin_get_by_name(GST_BIN(pipeline), "demux");
        probe.sink = gst_bin_get_by_name(GST_BIN(pipeline), "ts_sink");
        REQUIRE(demux_element != nullptr);
        REQUIRE(probe.sink != nullptr);
        g_signal_connect(
            demux_element, "pad-added", G_CALLBACK(on_pad_added), &probe);
        gst_object_unref(demux_element);
        gst_object_unref(probe.sink);
      });
  INFO("demux: " << demuxed.error);
  INFO("demux pad caps: " << probe.caps);
  REQUIRE(demuxed.ok);
  CHECK(probe.caps.rfind("video/x-av1", 0) == 0);
  CHECK(probe.caps.find("stream-format=(string)obu-stream")
        != std::string::npos);
  CHECK(probe.caps.find("alignment=(string)frame") != std::string::npos);
}

TEST_CASE("mpegtsmux refuses AV1 without enable-custom-mappings")
{
  gst_init(nullptr, nullptr);
  if (!have_gst_elements()) {
    SKIP("GStreamer AV1/mpegts elements are not installed");
  }

  const temp_dir tmp;
  const std::filesystem::path ts_file = tmp.path / "naive.ts";
  // Deliberately the mux element without the property ts_mux::element() sets:
  // if a future GStreamer gains a standard AV1 stream type this is the test
  // that says the custom mapping is no longer load-bearing.
  const std::string mux = std::string("videotestsrc num-buffers=10 ! ")
      + "video/x-raw,width=320,height=240,framerate=25/1 ! av1enc ! av1parse ! "
      + std::string(ts_mux::av1_frame_caps) + " ! mpegtsmux alignment=7 ! "
        "filesink location="
      + ts_file.string();

  const run_result muxed = run_pipeline(mux);
  INFO("mux: " << muxed.error);
  CHECK_FALSE(muxed.ok);
  CHECK(muxed.error.find("mapping is not supported") != std::string::npos);
}
