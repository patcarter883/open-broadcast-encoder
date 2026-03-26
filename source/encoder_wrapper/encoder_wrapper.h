#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// C ABI Type Definitions (mirroring C++ types from lib.h)
// ============================================================================

typedef enum {
  C_INPUT_NDI = 0,
  C_INPUT_RTSP = 1,
  C_INPUT_SRT = 2,
  C_INPUT_FILE = 3
} CInputMode;

typedef enum {
  C_CODEC_H264 = 0,
  C_CODEC_H265 = 1,
  C_CODEC_VP9 = 2,
  C_CODEC_AV1 = 3
} CCodec;

typedef enum {
  C_ENCODER_NVIDIA = 0,
  C_ENCODER_AMD = 1,
  C_ENCODER_INTEL = 2,
  C_ENCODER_LIBX264 = 3,
  C_ENCODER_LIBX265 = 4
} CEncoder;

typedef enum {
  C_TRANSPORT_RIST = 0,
  C_TRANSPORT_SRT = 1,
  C_TRANSPORT_RTMP = 2
} CTransportProtocol;

// Input configuration
typedef struct {
  CInputMode mode;
  char url[1024];
  char ndi_name[256];
  uint16_t port;
} CInputConfig;

// Encode configuration
typedef struct {
  CCodec codec;
  CEncoder encoder;
  int32_t width;
  int32_t height;
  int32_t bitrate_kbps;
  int32_t framerate_num;
  int32_t framerate_den;
  char preset[64];
} CEncodeConfig;

// Output/Stream configuration
typedef struct {
  char stream_id[256];
  char destination_url[1024];
  CTransportProtocol protocol;
  int32_t port;
  char sdp_file[1024];
} CStreamConfig;

// Stream statistics
typedef struct {
  char stream_id[256];
  int64_t packets_sent;
  int64_t bytes_sent;
  int64_t packets_lost;
  int64_t rtt_us;
  double bitrate_kbps;
} CStreamStats;

// Cumulative statistics
typedef struct {
  int32_t num_streams;
  CStreamStats streams[16];
  int64_t uptime_ms;
} CCumulativeStats;

// NDI device info
typedef struct {
  char id[256];
  char name[256];
  char hostname[256];
  char ip[64];
} CNDIDevice;

// ============================================================================
// Callback types
// ============================================================================

typedef void (*LogCallback)(const char* level, const char* message);
typedef void (*StatsCallback)(const CCumulativeStats* stats);
typedef void (*NDIDevicesCallback)(const CNDIDevice* devices, int32_t count);

// ============================================================================
// C API Functions
// ============================================================================

// Initialization
int32_t encoder_wrapper_init(LogCallback log_cb, StatsCallback stats_cb);
int32_t encoder_wrapper_destroy(void);

// Configuration
int32_t encoder_set_input_config(const CInputConfig* cfg);
int32_t encoder_set_encode_config(const CEncodeConfig* cfg);

// Stream management
int32_t encoder_add_stream(const CStreamConfig* cfg);
int32_t encoder_remove_stream(const char* stream_id);

// Control
int32_t encoder_start(LogCallback on_encode_log);
int32_t encoder_stop(void);
int32_t encoder_set_bitrate(int32_t bitrate_kbps);

// Query
int32_t encoder_get_cumulative_stats(CCumulativeStats* out);

// NDI
int32_t ndi_refresh_devices(NDIDevicesCallback cb);

// SDP file handling
int32_t encoder_open_sdp_file(const char* path);

// Preview (TODO: implement full preview pipeline)
int32_t encoder_start_preview(void);
int32_t encoder_stop_preview(void);

#ifdef __cplusplus
}
#endif
