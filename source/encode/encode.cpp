// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "encode/encode.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include "encode/capture_input.h"
#include "encode/capture_source.h"
#include "encode/raw_format.h"
#include "encode/raw_local.h"

using std::string;

encode::encode(const input_config& input_config,
               const encode_config& encode_config,
               const receiver_control_config& receiver_config,
               std::shared_ptr<std::atomic<bool>> run_flag,
               std::function<void(const std::string&)> log_func)
    : encoder_running {false}
    , run_flag {std::move(run_flag)}
    , log_func {std::move(log_func)}
    , input_c {input_config}
    , encode_c {encode_config}
    , receiver_c {receiver_config}
{
}

encode::~encode()
{
  if (this->run_flag) {
    *this->run_flag = false;
  }
  this->encoder_running = false;

  for (auto& t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  this->clear_pipeline_state();
}

void encode::clear_pipeline_state()
{
  // The reader pushes into the appsrcs below, so it stops first -- and here,
  // rather than in the callers, so both stop_encode_thread() and the destructor
  // get the ordering for free.
  if (this->raw_reader) {
    this->raw_reader->stop();
    this->raw_reader.reset();
  }
  // Same rule for the capture reader: it pushes into the appsrc below, so it
  // stops before the pipeline (and that appsrc) is torn down.
  if (this->capture_reader) {
    this->capture_reader->stop();
    this->capture_reader.reset();
  }

  std::lock_guard<std::mutex> guard(this->pipeline_mutex);
  if (this->pipeline_cleaned_up.load(std::memory_order_acquire)) {
    return;
  }

  if (this->datasrc_pipeline != nullptr) {
    gst_element_set_state(this->datasrc_pipeline, GST_STATE_NULL);
  }

  // Unref in reverse-dependency order. Elements first (each holds a ref from
  // gst_bin_get_by_name), bus next, then the pipeline itself.
  if (this->video_encoder != nullptr) {
    gst_object_unref(this->video_encoder);
    this->video_encoder = nullptr;
  }
  if (this->video_sink != nullptr) {
    gst_object_unref(this->video_sink);
    this->video_sink = nullptr;
  }
  if (this->audio_sink != nullptr) {
    gst_object_unref(this->audio_sink);
    this->audio_sink = nullptr;
  }
  if (this->video_queue != nullptr) {
    gst_object_unref(this->video_queue);
    this->video_queue = nullptr;
  }
  if (this->audio_queue != nullptr) {
    gst_object_unref(this->audio_queue);
    this->audio_queue = nullptr;
  }
  if (this->raw_video_src != nullptr) {
    gst_object_unref(this->raw_video_src);
    this->raw_video_src = nullptr;
  }
  if (this->raw_audio_src != nullptr) {
    gst_object_unref(this->raw_audio_src);
    this->raw_audio_src = nullptr;
  }
  if (this->capture_ts_src != nullptr) {
    gst_object_unref(this->capture_ts_src);
    this->capture_ts_src = nullptr;
  }
  if (this->bus != nullptr) {
    gst_object_unref(this->bus);
    this->bus = nullptr;
  }
  if (this->datasrc_pipeline != nullptr) {
    gst_object_unref(GST_OBJECT(this->datasrc_pipeline));
    this->datasrc_pipeline = nullptr;
    log("Stopping pipeline.\n");
  }

  this->pipeline_cleaned_up.store(true, std::memory_order_release);
}

void encode::pipeline_build_source()
{
  const auto* const sdp =
      "v=0\n"
      "o=- 1443716955 1443716955 IN IP4 127.0.0.1\n"
      "s=st2110 stream\n"
      "t=0 0\n"
      "a=recvonly\n"
      "\n"
      "m=video 20000 RTP/AVP 102\n"
      "c=IN IP4 127.0.0.1/8\n"
      "a=rtpmap:102 raw/90000\n"
      "a=fmtp:102 sampling=YCbCr-4:2:2; width=1920; height=1080; "
      "exactframerate=30000/1000; depth=10; TCS=SDR; colorimetry=BT709; "
      "PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN;\n"
      "a=mediaclk:direct=0\n"
      "a=ts-refclk:ptp=IEEE1588-2008:00-02-c5-ff-fe-21-60-5c:127\n";

  switch (input_c.selected_input_mode) {
    case input_mode::testsrc:
      this->pipeline_str = "";
      break;
    case input_mode::mpegts:
      // udpsrc's `caps` property defaults to application/x-udp, which cannot
      // negotiate with tsparse: the pipeline then dies instantly with
      // "streaming stopped, reason not-linked" and no frame ever reaches the
      // encoder (the RIST sender still connects, so it looks half-alive).
      // Pin the caps to the MPEG-TS shape we actually ingest.
      this->pipeline_str = std::format(
          "udpsrc port={} caps=video/mpegts,systemstream=true "
          "buffer-size=1000000 mtu=45000 ! tsparse "
          "set-timestamps=true ! tsdemux latency=10 "
          "name=demux ",
          input_c.selected_input);
      break;
    case input_mode::sdp:

      this->pipeline_str = std::format(
          "sdpsrc sdp=\"{}\" "
          "name=demux ",
          sdp);
      break;
    case input_mode::ndi:
      this->pipeline_str = std::format(
          "ndisrc do-timestamp=true ndi-name=\"{}\" ! ndisrcdemux name=demux ",
          input_c.selected_input);
      break;
    case input_mode::raw_local:
      // The OBS raw output plugin is the TCP *client*, so the socket is owned
      // by raw_local_input and these appsrcs are the far end of it. The
      // branches below start from `rawvideo.`/`rawaudio.`, the same way a tee
      // is branched in gst-launch syntax.
      //
      // No caps are set here on purpose: the geometry only exists once the
      // stream header has been read, so raw_local_input sets them then. Both
      // appsrcs are live and time-stamped by us (do-timestamp=false), because
      // OBS's own timestamps are the A/V sync and must survive untouched.
      // max-bytes bounds each queue, which is what applies back-pressure to
      // OBS instead of growing this process without limit.
      this->pipeline_str =
          "appsrc name=rawvideo is-live=true format=time do-timestamp=false "
          "max-bytes=16777216 "
          "appsrc name=rawaudio is-live=true format=time do-timestamp=false "
          "max-bytes=1048576 ";
      break;
    case input_mode::jpegxs_capture:
      // MC4: the JPEG XS camera link over the LAN. The node muxes JPEG XS into
      // MPEG-TS and sends it UNICAST to an ingest point, so this side LISTENS
      // on the stream port and capture_input (below, at Start) binds the
      // interface that routes to the selected camera and pushes each datagram
      // into `capturets`. From tsparse onward this is deliberately the SAME
      // fragment the mpegts mode builds, so tsdemux -> decodebin3 ->
      // svtjpegxsdec (the ONE JPEG XS decode path) is reused, not reimplemented
      // (MC4.2).
      //
      // `do-timestamp=true` because the datagrams arrive without timestamps and
      // this appsrc is format=time; tsparse set-timestamps=true then derives
      // the real ones from the multiplex's PCR.
      this->pipeline_str =
          "appsrc name=capturets is-live=true format=time do-timestamp=true "
          "caps=video/mpegts,systemstream=true max-bytes=16777216 "
          "! tsparse set-timestamps=true ! tsdemux latency=10 name=demux ";
      break;
    case input_mode::none:
      break;
  }
}

void encode::pipeline_build_sink()
{
  // alignment = TS packets per buffer = bytes per RIST datagram (n*188). Lower
  // it for low-MTU cellular links so RIST packets don't get IP-fragmented.
  // Clamp to a sane range; <1 would make mpegtsmux auto-size (= large buffers).
  const int alignment = std::clamp(
      encode_c.mpegts_alignment.load(std::memory_order_relaxed), 1, 7);
  // enable-custom-mappings=true is REQUIRED to mux AV1 (and VP9): GStreamer has
  // no standardised MPEG-TS stream type for them, so mpegtsmux otherwise fails
  // with "AV1 requires enabling custom mapping". No-op for H.264/H.265.
  this->pipeline_str += std::format(
      " appsink name=video_sink "
      "mpegtsmux alignment={} enable-custom-mappings=true name=tsmux "
      "! video_sink. ",
      alignment);
}

void encode::pipeline_build_video_demux()
{
  switch (input_c.selected_input_mode) {
    case input_mode::testsrc:
      this->pipeline_str +=
          " videotestsrc is-live=true pattern=smpte ! videoconvert !";
      break;
    case input_mode::ndi:
      this->pipeline_str += " demux.video ! queue silent=true ! videoconvert !";
      break;
    case input_mode::sdp:
      this->pipeline_str +=
          " demux. ! rtpvrawdepay ! queue silent=true ! videoconvert !";
      break;
    case input_mode::raw_local:
      // Already uncompressed, so straight to conversion: nothing to depay or
      // decode. videoconvert then hands the encoder whatever format it wants.
      this->pipeline_str += " rawvideo. ! queue silent=true ! videoconvert !";
      break;
    default:
      // Deliberately no `demux.` here. The MPEG-TS demux's pads are dynamic and
      // tsdemux will not resolve two any-pad delayed links (the second fails
      // with "failed delayed linking pad video ... to some pad of GstQueue"),
      // so the demux is left unlinked by the parser and these branches are
      // linked by caps in link_demux_pad().
      this->pipeline_str +=
          " queue name=vqueue silent=true ! decodebin3 ! videoconvert !";
      break;
  }
}

void encode::pipeline_build_audio_demux()
{
  switch (input_c.selected_input_mode) {
    case input_mode::testsrc:
      this->pipeline_str +=
          " audiotestsrc is-live=true wave=sine ! audioconvert ! audioresample "
          "!";
      break;
    case input_mode::ndi:
      this->pipeline_str +=
          " demux.audio ! queue silent=true ! audioresample ! audioconvert !";
      break;
    case input_mode::sdp:
      this->pipeline_str +=
          " demux. ! rtpL24depay ! queue silent=true ! audioresample ! "
          "audioconvert !";
      break;
    case input_mode::raw_local:
      // OBS hands over planar float32; resample/convert let avenc_aac take it.
      this->pipeline_str +=
          " rawaudio. ! queue silent=true ! audioresample ! audioconvert !";
      break;
    default:
      // See the video-demux case: linked by caps in link_demux_pad().
      this->pipeline_str +=
          " queue name=aqueue silent=true ! decodebin3 ! audioresample ! "
          "audioconvert !";
      break;
  }
}

void encode::pipeline_build_audio_encoder()
{
  this->pipeline_str += " avenc_aac ! aacparse ";
}

namespace
{
// Video-encoder fragment templates indexed by [encoder][codec].
//
// Row order follows enum class encoder: amd, qsv, nvenc, software.
// Column order follows enum class codec: h264, h265, av1.
//
// Each template is byte-for-byte identical to the string the corresponding
// pipeline_build_<vendor>_<codec>_encoder() function previously emitted
// (including incidental double-spaces). The single "{}" is the bitrate.
constexpr int kEncoderCount = 4;
constexpr int kCodecCount = 3;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
constexpr std::string_view kEncoderTemplates[kEncoderCount][kCodecCount] = {
// encoder::amd
//
// AMF (amfh264enc / amfh265enc / amfav1enc) is Windows-only. Everywhere
// else the same AMD hardware is driven through VAAPI instead
// (vah264enc / vah265enc / vaav1enc), which is also the encoder the
// receiver's transcode tier uses. This is not a fallback: with the AMF
// names, `encoder=amd` builds a pipeline whose elements do not exist on
// Linux, so it can never reach PLAYING.
#ifdef _WIN32
    {
        // codec::h264
        "amfh264enc name=videncoder  bitrate={} rate-control=cbr "
        "usage=low-latency preset=quality pre-encode=true pa-hqmb-mode=auto ! "
        "video/x-h264,profile=high ! h264parse "
        "config-interval=1 ",
        // codec::h265
        "amfh265enc name=videncoder bitrate={} rate-control=cbr "
        "usage=low-latency preset=quality pre-encode=true pa-hqmb-mode=auto ! "
        "h265parse config-interval=1 ",
        // codec::av1
        "amfav1enc name=videncoder bitrate={} rate-control=cbr "
        "usage=low-latency preset=high-quality  pre-encode=true "
        "pa-hqmb-mode=auto "
        "! av1parse ! video/x-av1,stream-format=obu-stream,alignment=frame ",
    },
#else
    {
        // codec::h264
        "vah264enc name=videncoder bitrate={} rate-control=cbr "
        "key-int-max=60 ! video/x-h264,profile=high ! h264parse "
        "config-interval=1 ",
        // codec::h265
        "vah265enc name=videncoder bitrate={} rate-control=cbr "
        "key-int-max=60 ! h265parse config-interval=1 ",
        // codec::av1
        "vaav1enc name=videncoder bitrate={} rate-control=cbr "
        "key-int-max=60 ! av1parse "
        "! video/x-av1,stream-format=obu-stream,alignment=frame ",
    },
#endif
    // encoder::qsv
    {
        // codec::h264
        "qsvh264enc name=videncoder  bitrate={} rate-control=cbr "
        "target-usage=1 ! h264parse "
        "config-interval=1 ",
        // codec::h265
        "qsvh265enc name=videncoder bitrate={} rate-control=cbr "
        "target-usage=1 ! h265parse "
        "config-interval=1 ",
        // codec::av1
        "qsvav1enc name=videncoder bitrate={} rate-control=cbr "
        "target-usage=1 gop-size=120 ! av1parse "
        "! video/x-av1,stream-format=obu-stream,alignment=frame ",
    },
    // encoder::nvenc
    {
        // codec::h264
        "nvh264enc name=videncoder bitrate={} rc-mode=cbr-hq "
        "preset=low-latency-hq ! h264parse config-interval=1 ",
        // codec::h265
        "nvh265enc name=videncoder bitrate={} rc-mode=cbr-hq "
        "preset=low-latency-hq ! h265parse config-interval=1 ",
        // codec::av1
        "nvav1enc name=videncoder bitrate={} rc-mode=cbr preset=low-latency-hq "
        "! av1parse ! video/x-av1,stream-format=obu-stream,alignment=frame ",
    },
    // encoder::software
    {
        // codec::h264
        "x264enc name=videncoder bitrate={} "
        "speed-preset=fast tune=zerolatency ! h264parse config-interval=1 ",
        // codec::h265
        "x265enc name=videncoder bitrate={} "
        "speed-preset=fast tune=zerolatency ! h265parse config-interval=1 ",
        // codec::av1
        "rav1enc name=videncoder bitrate={} speed-preset=8 tile-cols=2 "
        "tile-rows=2 ! av1parse "
        "! video/x-av1,stream-format=obu-stream,alignment=frame ",
    },
};
}  // namespace

void encode::pipeline_build_video_encoder()
{
  // Map encoder enum to a row, with unknown encoders falling back to software
  // (matches the old pipeline_build_video_encoder default branch).
  int enc_index = 0;
  switch (encode_c.selected_encoder) {
    case encoder::amd:
      enc_index = 0;
      break;
    case encoder::qsv:
      enc_index = 1;
      break;
    case encoder::nvenc:
      enc_index = 2;
      break;
    default:
      enc_index = 3;  // software
      break;
  }

  // Map codec enum to a column, with the 'default' codec falling back to h264
  // (matches the old per-vendor dispatcher default branch).
  int codec_index = 0;
  switch (encode_c.selected_codec) {
    case codec::h265:
      codec_index = 1;
      break;
    case codec::av1:
      codec_index = 2;
      break;
    default:
      codec_index = 0;  // h264
      break;
  }

  const std::string_view tmpl = kEncoderTemplates[enc_index][codec_index];
  const int bitrate_value = encode_c.bitrate.load(std::memory_order_relaxed);
  this->pipeline_str +=
      std::vformat(tmpl, std::make_format_args(bitrate_value));
}

void encode::pipeline_build_audio_payloader()
{
  this->pipeline_str += "! queue silent=true ! tsmux. ";
}

void encode::pipeline_build_video_payloader()
{
  this->pipeline_str += "! queue silent=true ! tsmux. ";
}

// Connect the pad-added handler for the MPEG-TS demux. Scoped to that path by
// the named branch queues: only pipeline_build_{video,audio}_demux's default
// case creates them, so ndi/sdp/testsrc keep their existing parse-time links.
// Only the VIDEO queue is required: a video-only input (the JPEG XS camera
// link) omits the audio branch entirely, so audio_queue is legitimately nullptr
// there.
void encode::link_demux_pads()
{
  if (this->video_queue == nullptr) {
    return;
  }
  GstElement* demux =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "demux");
  if (demux == nullptr) {
    return;
  }
  // The signal lives on the element, which is owned by the pipeline, so
  // dropping our ref here does not disconnect it.
  g_signal_connect(
      demux, "pad-added", G_CALLBACK(&encode::on_demux_pad_added), this);
  gst_object_unref(demux);
}

void encode::on_demux_pad_added(GstElement* /*demux*/,
                                GstPad* pad,
                                gpointer user_data)
{
  static_cast<encode*>(user_data)->link_demux_pad(pad);
}

// tsdemux emits its pads with caps already set, so route each one to the
// matching branch queue. Queueing the link (rather than requesting a named pad
// up front) is what makes this reliable: an any-pad request can hand the video
// pad to the audio branch, and two any-pad requests fail to link at all.
void encode::link_demux_pad(GstPad* pad)
{
  GstCaps* caps = gst_pad_get_current_caps(pad);
  if (caps == nullptr) {
    caps = gst_pad_query_caps(pad, nullptr);
  }
  const GstStructure* structure =
      (caps != nullptr) ? gst_caps_get_structure(caps, 0) : nullptr;
  const gchar* media_type =
      (structure != nullptr) ? gst_structure_get_name(structure) : nullptr;
  if (media_type == nullptr) {
    if (caps != nullptr) {
      gst_caps_unref(caps);
    }
    return;
  }

  GstElement* queue = nullptr;
  // tsdemux surfaces JPEG XS as `image/x-jxsc`, not `video/*` (a JPEG XS
  // elementary stream is a still-image codec to the multiplex). Route it to the
  // VIDEO branch so decodebin3 -> svtjpegxsdec runs -- the same branch, and so
  // the same ONE decode path, that a `video/` elementary stream uses. Without
  // this the pad is never linked, tsdemux's push returns NOT_LINKED and the
  // pipeline dies with "streaming stopped, reason not-linked".
  if (g_str_has_prefix(media_type, "video/")
      || g_str_has_prefix(media_type, "image/x-jxsc"))
  {
    queue = this->video_queue;
  } else if (g_str_has_prefix(media_type, "audio/")) {
    queue = this->audio_queue;
  }

  if (queue != nullptr) {
    GstPad* sink_pad = gst_element_get_static_pad(queue, "sink");
    if (sink_pad != nullptr) {
      if (!gst_pad_is_linked(sink_pad)) {
        const GstPadLinkReturn ret = gst_pad_link(pad, sink_pad);
        log(std::format("demux pad '{}' ({}) -> {}: {}",
                        GST_PAD_NAME(pad),
                        media_type,
                        GST_ELEMENT_NAME(queue),
                        gst_pad_link_get_name(ret)));
      }
      gst_object_unref(sink_pad);
    }
  }
  gst_caps_unref(caps);
}

void encode::build_pipeline()
{
  this->pipeline_build_source();
  this->pipeline_build_sink();
  // The JPEG XS camera link carries video only. mpegtsmux is a collectpads
  // aggregator: a request pad that is LINKED but never receives data stalls the
  // muxer, so a silent audio branch would stall the entire output (the input
  // has no audio to give it). Omit the audio branch for a video-only input --
  // the same way the input itself omits audio -- so tsmux has only the pad that
  // is actually fed.
  if (input_c.selected_input_mode != input_mode::jpegxs_capture) {
    this->pipeline_build_audio_demux();
    this->pipeline_build_audio_encoder();
    this->pipeline_build_audio_payloader();
  }
  this->pipeline_build_video_demux();
  this->pipeline_build_video_encoder();
  this->pipeline_build_video_payloader();
}

void encode::parse_pipeline()
{
  std::lock_guard<std::mutex> guard(this->pipeline_mutex);
  this->datasrc_pipeline = nullptr;
  GError* error = nullptr;

  log(this->pipeline_str);

  this->datasrc_pipeline = gst_parse_launch(this->pipeline_str.c_str(), &error);
  if (error != nullptr) {
    log(std::format("Parse Error: {}", error->message));
    g_clear_error(&error);
  }
  if (this->datasrc_pipeline == nullptr) {
    log("*** Bad datasrc_pipeline ***\n");
    return;
  }

  this->video_encoder =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "videncoder");
  this->video_sink =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "video_sink");
  this->audio_sink =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "audio_sink");

  this->video_queue =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "vqueue");
  this->audio_queue =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "aqueue");
  // Null unless the mode is raw_local; the names are only in that pipeline.
  this->raw_video_src =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "rawvideo");
  this->raw_audio_src =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "rawaudio");
  // Null unless the mode is jpegxs_capture.
  this->capture_ts_src =
      gst_bin_get_by_name(GST_BIN(this->datasrc_pipeline), "capturets");
  this->link_demux_pads();

  this->bus = gst_element_get_bus(this->datasrc_pipeline);
  this->pipeline_cleaned_up.store(false, std::memory_order_release);
}

void encode::play_pipeline()
{
  encoder_running = true;
  const std::chrono::milliseconds duration(1);
  while (run_flag && run_flag->load() && encoder_running.load()) {
    GstBus* local_bus = nullptr;
    {
      std::lock_guard<std::mutex> guard(this->pipeline_mutex);
      if (this->bus != nullptr) {
        local_bus = this->bus;
        gst_object_ref(local_bus);
      }
    }
    if (local_bus == nullptr) {
      break;
    }
    GstMessage* msg = gst_bus_timed_pop(local_bus, GST_MSECOND);
    gst_object_unref(local_bus);
    if (msg != nullptr) {
      this->handle_gstreamer_message(msg);
      gst_message_unref(msg);
    } else {
      std::this_thread::sleep_for(duration);
    }
  }
}

auto encode_state_text(encode_state state) -> const char*
{
  switch (state) {
    case encode_state::idle:
      return "Idle";
    case encode_state::starting:
      return "Starting";
    case encode_state::streaming:
      return "Streaming";
    case encode_state::failed:
      return "FAILED";
  }
  return "Unknown";
}

void encode::run_encode_thread()
{
  this->state.store(encode_state::starting, std::memory_order_relaxed);
  this->build_pipeline();
  this->parse_pipeline();
  if (this->datasrc_pipeline == nullptr) {
    log("Refusing to start: pipeline failed to parse.\n");
    this->state.store(encode_state::failed, std::memory_order_relaxed);
    return;
  }
  log("Playing pipeline.\n");
  if (this->run_flag) {
    *this->run_flag = true;
  }
  gst_element_set_state(this->datasrc_pipeline, GST_STATE_PLAYING);
  if (this->input_c.selected_input_mode == input_mode::raw_local) {
    this->start_raw_reader();
  }
  if (this->input_c.selected_input_mode == input_mode::jpegxs_capture) {
    this->start_capture_reader();
  }
  threads.emplace_back([this] { play_pipeline(); });
}

// The policy for the format OBS announces on the raw wire: an empty return
// accepts the stream, a non-empty one is the reason to refuse it (the reader
// logs that and drops the connection, so a corrected OBS rejoins the session).
auto encode::raw_format_verdict(std::uint32_t obs_format) -> std::string
{
  if (!obs_video_format_is_10bit(obs_format)) {
    return {};  // 8-bit: every codec and destination here carries it as-is
  }

  // H.264 has no 10-bit profile at any level, so there is nothing to negotiate.
  // Without this check the pipeline either fails caps negotiation, or a
  // videoconvert quietly drops the extra two bits, and neither says why.
  if (this->encode_c.selected_codec == codec::h264) {
    return "the OBS capture is 10-bit but the selected codec is H.264, which "
           "has no 10-bit profile: select H.265 or AV1, or set OBS's Color "
           "Format to NV12";
  }

  // The stream can be carried; a destination may still not accept it. Warn and
  // carry on -- the operator's decision is warn-but-allow -- naming the pair
  // actually in use.
  for (const auto& dest : this->receiver_c.destinations) {
    if (dest.proto == output_proto::rtmp || dest.proto == output_proto::rtmps) {
      this->log(
          "warning: 10-bit capture with an RTMP destination: RTMP carries "
          "10-bit only as Enhanced RTMP (eflvmux) and platform support for it "
          "is uneven. SRT and RIST carry it cleanly, and an H.264 destination "
          "cannot carry it at all.\n");
      break;
    }
  }
  return {};
}

void encode::start_raw_reader()
{
  if (this->raw_video_src == nullptr || this->raw_audio_src == nullptr) {
    log("*** raw_local: pipeline has no rawvideo/rawaudio appsrcs; not "
        "starting the reader ***\n");
    return;
  }

  // The listen-port field is shared with the MPEG-TS ingest; empty means the
  // OBS plugin's default target.
  std::uint16_t port = raw_local_input::default_port;
  if (!this->input_c.selected_input.empty()) {
    try {
      const int parsed = std::stoi(this->input_c.selected_input);
      if (parsed > 0 && parsed <= 65535) {
        port = static_cast<std::uint16_t>(parsed);
      } else {
        log(std::format("raw_local: port {} is out of range; using {}\n",
                        parsed,
                        raw_local_input::default_port));
      }
    } catch (const std::exception&) {
      log(std::format("raw_local: '{}' is not a valid port; using {}\n",
                      this->input_c.selected_input,
                      raw_local_input::default_port));
    }
  }

  this->raw_reader = std::make_unique<raw_local_input>(
      this->raw_video_src,
      this->raw_audio_src,
      port,
      [this](const std::string& msg) { this->log(msg); },
      [this](std::uint32_t obs_format)
      { return this->raw_format_verdict(obs_format); });
  this->raw_reader->start();
}

// MC4: the LAN reader for a JPEG XS capture source. The port is the stream
// port the camera is sending TO (the listener field, defaulting to the node's
// 5000); the address is the camera chosen in the LAN picker, used to bind the
// interface that routes to it and to drop datagrams from any other sender. Both
// may be absent -- the manual fallback (DT-19): wildcard bind, accept any
// sender. The reader is a member so clear_pipeline_state() stops it before the
// appsrc it pushes into is unreffed.
void encode::start_capture_reader()
{
  if (this->capture_ts_src == nullptr) {
    log("*** jpegxs_capture: pipeline has no capturets appsrc; not starting "
        "the reader ***\n");
    return;
  }

  const std::uint16_t port =
      capture::parse_stream_port(this->input_c.selected_input);
  if (this->input_c.selected_input.empty()) {
    log(std::format("jpegxs_capture: no stream port given; using {}\n", port));
  }

  this->log(std::format(
      "jpegxs_capture: capture source '{}' ({})\n",
      this->input_c.capture_name.empty() ? std::string {"<manual>"}
                                         : this->input_c.capture_name,
      this->input_c.capture_address.empty() ? std::string {"any address"}
                                            : this->input_c.capture_address));

  this->capture_reader = std::make_unique<capture_input>(
      this->capture_ts_src,
      this->input_c.capture_address,
      port,
      [this](const std::string& msg) { this->log(msg); });
  this->capture_reader->start();
}

void encode::stop_encode_thread()
{
  if (this->run_flag) {
    *this->run_flag = false;
  }
  this->encoder_running = false;

  for (auto& t : this->threads) {
    if (t.joinable()) {
      t.join();
    }
  }
  this->threads.clear();

  this->state.store(encode_state::idle, std::memory_order_relaxed);
  this->clear_pipeline_state();
}

void encode::handle_gst_message_error(GstMessage* message)
{
  GError* err = nullptr;
  gchar* debug_info = nullptr;
  gst_message_parse_error(message, &err, &debug_info);
  log("\nReceived error from datasrc_pipeline...\n");
  log(std::format("Error received from element {}: {}\n",
                  GST_OBJECT_NAME(message->src),
                  (err != nullptr) ? err->message : "unknown"));
  log(std::format("Debugging information: {}\n",
                  (debug_info != nullptr) ? debug_info : "none"));
  log("*** Encode stopped: the pipeline failed (see the error above). "
      "Press Stop, fix the cause, then Start. ***\n");
  g_clear_error(&err);
  g_free(debug_info);
  encoder_running = false;
  this->state.store(encode_state::failed, std::memory_order_relaxed);
}

void encode::handle_gst_message_eos(GstMessage* /*message*/)
{
  log("\nReceived EOS from pipeline...\n");
  log("*** Encode stopped: the input ended (EOS). ***\n");
  encoder_running = false;
  this->state.store(encode_state::failed, std::memory_order_relaxed);
}

void encode::handle_gstreamer_message(GstMessage* message)
{
  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR:
      this->handle_gst_message_error(message);
      break;
    case GST_MESSAGE_EOS:
      this->handle_gst_message_eos(message);
      break;
    default:
      break;
  }
}

auto encode::pull_from_sink(GstElement* encode::*sink_field) -> buffer_data
{
  GstElement* sink = nullptr;
  {
    std::lock_guard<std::mutex> guard(this->pipeline_mutex);
    sink = this->*sink_field;
    if (sink != nullptr) {
      gst_object_ref(sink);
    }
  }
  if (sink == nullptr) {
    return buffer_data {};
  }

  GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
  gst_object_unref(sink);

  if (sample == nullptr) {
    return buffer_data {};
  }
  GstBuffer* buffer = gst_sample_get_buffer(sample);
  if (buffer == nullptr) {
    gst_sample_unref(sample);
    return buffer_data {};
  }

  GstMapInfo info;
  if (gst_buffer_map(buffer, &info, GST_MAP_READ) == 0) {
    gst_sample_unref(sample);
    return buffer_data {};
  }

  buffer_data result;
  result.buf_size = info.size;
  if (info.data != nullptr && info.size > 0) {
    result.buf_data.assign(info.data, info.data + info.size);
  }
  gst_buffer_unmap(buffer, &info);
  gst_sample_unref(sample);
  return result;
}

auto encode::pull_video_buffer() -> buffer_data
{
  return pull_from_sink(&encode::video_sink);
}

auto encode::pull_audio_buffer() -> buffer_data
{
  return pull_from_sink(&encode::audio_sink);
}

bool encode::source_fps(std::uint32_t& num, std::uint32_t& den)
{
  std::lock_guard<std::mutex> guard(this->pipeline_mutex);
  // Read the rate from the encoder's own sink caps: negotiated for every input
  // type alike (raw/OBS, capture, SDP, MPEG-TS, NDI, test), and without
  // reaching into a reader's private state. False before the pipeline has
  // negotiated -- the caller then leaves the portal's value alone rather than
  // guessing.
  if (this->video_encoder == nullptr) {
    return false;
  }
  GstPad* pad = gst_element_get_static_pad(this->video_encoder, "sink");
  if (pad != nullptr) {
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (caps != nullptr) {
      bool found = false;
      if (gst_caps_get_size(caps) > 0) {
        const GstStructure* structure = gst_caps_get_structure(caps, 0);
        gint cap_num = 0;
        gint cap_den = 1;
        if (structure != nullptr
            && gst_structure_get_fraction(
                structure, "framerate", &cap_num, &cap_den)
            && cap_num > 0 && cap_den > 0)
        {
          num = static_cast<std::uint32_t>(cap_num);
          den = static_cast<std::uint32_t>(cap_den);
          found = true;
        }
      }
      gst_caps_unref(caps);
      gst_object_unref(pad);
      return found;
    }
    gst_object_unref(pad);
  }
  return false;
}

void encode::set_encode_bitrate(int new_bitrate)
{
  if (new_bitrate <= 0) {
    return;
  }
  std::lock_guard<std::mutex> guard(this->pipeline_mutex);
  if (this->video_encoder == nullptr) {
    return;
  }
  g_object_set(G_OBJECT(this->video_encoder), "bitrate", new_bitrate, nullptr);
}

void encode::log(const std::string& msg) const
{
  if (log_func) {
    log_func(msg);
  }
}