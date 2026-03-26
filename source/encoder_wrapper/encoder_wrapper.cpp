#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gst/gst.h>

#include "../lib/lib.h"
#include "../encode/encode.h"
#include "../transport/transport_manager.h"
#include "../stats/statistics_aggregator.h"
#include "../ndi_input/ndi_input.h"
#include "encoder_wrapper.h"

namespace {

// ============================================================================
// File-scope state management
// ============================================================================

struct wrapper_state {
  library app;
  transport_manager g_transport_manager;
  statistics_aggregator g_stats_aggregator;
  std::unique_ptr<encode> ptr_encoder;
  ndi_input* p_ndi;
  LogCallback g_log_cb;
  LogCallback g_encode_log_cb;
  StatsCallback g_stats_cb;
  std::mutex state_mutex;
};

static wrapper_state* g_state = nullptr;

// ============================================================================
// Helper functions
// ============================================================================

static input_mode c_input_mode_to_cpp(CInputMode mode) {
  switch (mode) {
    case C_INPUT_NDI:
      return input_mode::ndi;
    case C_INPUT_RTSP:
      return input_mode::mpegts;
    case C_INPUT_SRT:
      return input_mode::mpegts;
    case C_INPUT_FILE:
      return input_mode::mpegts;
    default:
      return input_mode::none;
  }
}

static codec c_codec_to_cpp(CCodec codec_c) {
  switch (codec_c) {
    case C_CODEC_H264:
      return codec::h264;
    case C_CODEC_H265:
      return codec::h265;
    case C_CODEC_VP9:
    case C_CODEC_AV1:
      return codec::av1;
    default:
      return codec::h264;
  }
}

static encoder c_encoder_to_cpp(CEncoder enc) {
  switch (enc) {
    case C_ENCODER_NVIDIA:
      return encoder::nvenc;
    case C_ENCODER_AMD:
      return encoder::amd;
    case C_ENCODER_INTEL:
      return encoder::qsv;
    case C_ENCODER_LIBX264:
    case C_ENCODER_LIBX265:
      return encoder::software;
    default:
      return encoder::software;
  }
}

static transport_protocol c_transport_to_cpp(CTransportProtocol proto) {
  switch (proto) {
    case C_TRANSPORT_RIST:
      return transport_protocol::rist;
    case C_TRANSPORT_SRT:
      return transport_protocol::srt;
    case C_TRANSPORT_RTMP:
      return transport_protocol::rtmp;
    default:
      return transport_protocol::rist;
  }
}

// ============================================================================
// C API Implementation
// ============================================================================

}  // namespace

extern "C" {

int32_t encoder_wrapper_init(LogCallback log_cb, StatsCallback stats_cb) {
  if (g_state != nullptr) {
    return -1;  // Already initialized
  }

  try {
    g_state = new wrapper_state();
    g_state->g_log_cb = log_cb;
    g_state->g_stats_cb = stats_cb;
    g_state->p_ndi = nullptr;

    // Log initialization
    if (log_cb != nullptr) {
      log_cb("info", "Encoder wrapper initialized");
    }

    return 0;
  } catch (const std::exception& e) {
    if (log_cb != nullptr) {
      log_cb("error", std::string("Failed to initialize encoder wrapper: ")
                        .append(e.what())
                        .c_str());
    }
    if (g_state != nullptr) {
      delete g_state;
      g_state = nullptr;
    }
    return -1;
  }
}

int32_t encoder_wrapper_destroy(void) {
  if (g_state == nullptr) {
    return -1;  // Not initialized
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    // Stop encoder if running
    if (g_state->ptr_encoder != nullptr) {
      g_state->ptr_encoder.reset();
    }

    // Clean up NDI
    if (g_state->p_ndi != nullptr) {
      delete g_state->p_ndi;
      g_state->p_ndi = nullptr;
    }

    if (g_state->g_log_cb != nullptr) {
      g_state->g_log_cb("info", "Encoder wrapper destroyed");
    }

    delete g_state;
    g_state = nullptr;

    return 0;
  } catch (const std::exception& e) {
    if (g_state != nullptr && g_state->g_log_cb != nullptr) {
      g_state->g_log_cb("error", std::string("Error destroying wrapper: ")
                                     .append(e.what())
                                     .c_str());
    }
    return -1;
  }
}

int32_t encoder_set_input_config(const CInputConfig* cfg) {
  if (g_state == nullptr || cfg == nullptr) {
    return -1;
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    g_state->app.input_config.selected_input_mode =
        c_input_mode_to_cpp(cfg->mode);

    if (cfg->mode == C_INPUT_NDI && cfg->ndi_name[0] != '\0') {
      g_state->app.input_config.selected_input = cfg->ndi_name;
    } else if (cfg->url[0] != '\0') {
      g_state->app.input_config.selected_input = cfg->url;
    }

    return 0;
  } catch (const std::exception& e) {
    return -1;
  }
}

int32_t encoder_set_encode_config(const CEncodeConfig* cfg) {
  if (g_state == nullptr || cfg == nullptr) {
    return -1;
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    g_state->app.encode_config.codec = c_codec_to_cpp(cfg->codec);
    g_state->app.encode_config.encoder = c_encoder_to_cpp(cfg->encoder);
    g_state->app.encode_config.width = cfg->width;
    g_state->app.encode_config.height = cfg->height;
    g_state->app.encode_config.bitrate = std::to_string(cfg->bitrate_kbps);
    g_state->app.encode_config.framerate = cfg->framerate_num / cfg->framerate_den;

    return 0;
  } catch (const std::exception& e) {
    return -1;
  }
}

int32_t encoder_add_stream(const CStreamConfig* cfg) {
  if (g_state == nullptr || cfg == nullptr) {
    return -1;
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    stream_config sc;
    sc.id = cfg->stream_id;
    sc.protocol = c_transport_to_cpp(cfg->protocol);
    sc.address = cfg->destination_url;

    g_state->app.streams.push_back(sc);

    return 0;
  } catch (const std::exception& e) {
    return -1;
  }
}

int32_t encoder_remove_stream(const char* stream_id) {
  if (g_state == nullptr || stream_id == nullptr) {
    return -1;
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    auto& streams = g_state->app.streams;
    auto it = std::find_if(
        streams.begin(), streams.end(),
        [stream_id](const stream_config& sc) { return sc.id == stream_id; });

    if (it != streams.end()) {
      streams.erase(it);
      return 0;
    }

    return -1;
  } catch (const std::exception& e) {
    return -1;
  }
}

int32_t encoder_start(LogCallback on_encode_log) {
  if (g_state == nullptr) {
    return -1;
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    g_state->g_encode_log_cb = on_encode_log;

    // Initialize GStreamer if not already done
    if (!gst_is_initialized()) {
      gst_init(nullptr, nullptr);
    }

    // Create encoder instance
    g_state->ptr_encoder =
        std::make_unique<encode>(g_state->app, &g_state->g_stats_aggregator);

    // Add streams to transport manager
    for (const auto& sc : g_state->app.streams) {
      // TODO: Connect streams to transport manager
    }

    if (g_state->g_log_cb != nullptr) {
      g_state->g_log_cb("info", "Encoder started");
    }

    return 0;
  } catch (const std::exception& e) {
    if (g_state->g_log_cb != nullptr) {
      g_state->g_log_cb("error", std::string("Failed to start encoder: ")
                                     .append(e.what())
                                     .c_str());
    }
    return -1;
  }
}

int32_t encoder_stop(void) {
  if (g_state == nullptr) {
    return -1;
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    if (g_state->ptr_encoder != nullptr) {
      g_state->ptr_encoder.reset();
    }

    if (g_state->g_log_cb != nullptr) {
      g_state->g_log_cb("info", "Encoder stopped");
    }

    return 0;
  } catch (const std::exception& e) {
    return -1;
  }
}

int32_t encoder_set_bitrate(int32_t bitrate_kbps) {
  if (g_state == nullptr) {
    return -1;
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    if (g_state->ptr_encoder != nullptr) {
      g_state->ptr_encoder->set_encode_bitrate(bitrate_kbps);
    }

    return 0;
  } catch (const std::exception& e) {
    return -1;
  }
}

int32_t encoder_get_cumulative_stats(CCumulativeStats* out) {
  if (g_state == nullptr || out == nullptr) {
    return -1;
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    // Clear output
    out->num_streams = 0;

    // Populate from statistics_aggregator
    // TODO: Implement full stats gathering

    return 0;
  } catch (const std::exception& e) {
    return -1;
  }
}

int32_t ndi_refresh_devices(NDIDevicesCallback cb) {
  if (g_state == nullptr || cb == nullptr) {
    return -1;
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    // TODO: Implement NDI device enumeration
    // For now, call with empty list
    CNDIDevice devices[1] = {0};
    cb(devices, 0);

    return 0;
  } catch (const std::exception& e) {
    return -1;
  }
}

int32_t encoder_open_sdp_file(const char* path) {
  if (g_state == nullptr || path == nullptr) {
    return -1;
  }

  try {
    std::lock_guard<std::mutex> lock(g_state->state_mutex);

    // TODO: Implement SDP file parsing

    return 0;
  } catch (const std::exception& e) {
    return -1;
  }
}

int32_t encoder_start_preview(void) {
  if (g_state == nullptr) {
    return -1;
  }

  // TODO: Implement full preview pipeline with separate GStreamer thread
  return 0;
}

int32_t encoder_stop_preview(void) {
  if (g_state == nullptr) {
    return -1;
  }

  // TODO: Implement preview cleanup
  return 0;
}

}  // extern "C"
