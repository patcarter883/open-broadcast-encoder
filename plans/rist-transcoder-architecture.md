# RIST Stream Receiver & Multi-Protocol Restreaming Transcoder Architecture

## Executive Summary

This document defines the high-level architecture for a RIST stream receiver and multi-protocol restreaming transcoder that integrates with [`open-broadcast-encoder`](../open-broadcast-encoder). The system receives RIST streams, transcodes video content, and restreams to multiple destinations simultaneously via RIST, SRT, RTMP, or SDP/RTP protocols.

---

## 1. System Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        RIST Transcoder System                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────────────────┐  │
│  │   RIST Input │───▶│  Transcoder  │───▶│      Buffer Fanout           │  │
│  │  (rist-cpp)  │    │ (GStreamer)  │    │  (reference-counted)         │  │
│  └──────────────┘    └──────────────┘    └──────────────────────────────┘  │
│                                                   │                         │
│           ┌───────────────────────────────────────┼───────────────────┐    │
│           ▼                   ▼                   ▼                   ▼    │
│    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌──────────┐  │
│    │RIST Output  │    │ SRT Output  │    │RTMP Output  │    │SDP Output│  │
│    │(rist-cpp)   │    │(GStreamer)  │    │(GStreamer)  │    │(GStreamer│  │
│    └─────────────┘    └─────────────┘    └─────────────┘    └──────────┘  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Module Structure

### 2.1 Directory Layout

```
rist-transcoder/
├── source/
│   ├── lib/
│   │   ├── CMakeLists.txt
│   │   └── lib.cppm                 # Core types, enums, config structs
│   ├── receiver/
│   │   ├── CMakeLists.txt
│   │   └── receiver.cppm            # RIST input receiver
│   ├── transcoder/
│   │   ├── CMakeLists.txt
│   │   └── transcoder.cppm          # GStreamer transcoding pipeline
│   ├── fanout/
│   │   ├── CMakeLists.txt
│   │   └── fanout.cppm              # Reference-counted buffer distribution
│   ├── output/
│   │   ├── CMakeLists.txt
│   │   ├── output_base.cppm         # Abstract output handler base
│   │   ├── output_rist.cppm         # RIST output implementation
│   │   ├── output_srt.cppm          # SRT output implementation
│   │   ├── output_rtmp.cppm         # RTMP output implementation
│   │   └── output_sdp.cppm          # SDP/RTP output implementation
│   ├── config/
│   │   ├── CMakeLists.txt
│   │   └── config.cppm              # JSON configuration management
│   ├── stats/
│   │   ├── CMakeLists.txt
│   │   └── stats.cppm               # Statistics and monitoring
│   ├── ui/
│   │   ├── CMakeLists.txt
│   │   ├── ui.cppm                  # FLTK UI components
│   │   └── ui.fld                   # Fluid UI definition
│   └── main.cpp                     # Application entry point
├── external/                        # Git submodules
│   ├── fltk/                        # FLTK GUI framework
│   ├── rist-cpp/                    # RIST protocol C++ wrapper
│   └── json/                        # JSON library (nlohmann/json)
├── cmake/
├── docs/
├── CMakeLists.txt
└── README.md
```

### 2.2 Module Dependencies

```
                    main.cpp
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
      library        ui           config
        │              │              │
   ┌────┴────┐    ┌────┴────┐    ┌────┴────┐
   │         │    │         │    │         │
receiver transcoder fanout  stats output_base
   │         │    │              │    │
   │         │    └──────┬───────┘    │
   │         │           │            │
   │         └───────────┤            │
   │                     │            │
   └─────────────────────┴────────────┘
                         │
              ┌─────────┼─────────┐
              ▼         ▼         ▼
        output_rist output_srt output_rtmp output_sdp
```

---

## 3. Core Module Specifications

### 3.1 [`library`](source/lib/lib.cppm) Module

**File**: `source/lib/lib.cppm`  
**Exports**: `library`

#### Enums

```cpp
export enum class input_protocol : std::uint8_t
{
  rist,      // RIST protocol input
  none
};

export enum class output_protocol : std::uint8_t
{
  rist,      // RIST protocol output
  srt,       // SRT protocol output
  rtmp,      // RTMP protocol output
  sdp,       // SDP/RTP output
  none
};

export enum class video_codec : std::uint8_t
{
  h264,      // H.264/AVC
  h265,      // H.265/HEVC
  av1,       // AV1
  copy       // Pass-through (no transcoding)
};

export enum class encoder_type : std::uint8_t
{
  nvidia,    // NVENC hardware encoder
  intel,     // QSV hardware encoder
  amd,       // AMF hardware encoder
  software   // Software encoder (x264/x265/rav1e)
};

export enum class output_state : std::uint8_t
{
  idle,
  connecting,
  connected,
  error,
  reconnecting
};
```

#### Core Data Structures

```cpp
// Reference-counted buffer for zero-copy fanout
export struct shared_buffer
{
  std::shared_ptr<std::vector<uint8_t>> data;
  uint64_t timestamp_pts{0};
  uint64_t timestamp_dts{0};
  uint64_t sequence_number{0};
  bool is_keyframe{false};
  std::chrono::steady_clock::time_point received_time;
};

// RIST receiver configuration
export struct rist_input_config
{
  std::string listen_address{"0.0.0.0"};
  uint16_t listen_port{5000};
  uint32_t buffer_min{245};
  uint32_t buffer_max{5000};
  uint32_t rtt_min{40};
  uint32_t rtt_max{500};
  uint32_t reorder_buffer{240};
  uint32_t session_timeout{5000};
  std::string psk;           // Pre-shared key for encryption (optional)
  std::string cname;         // CNAME for identification
};

// Transcoding configuration
export struct transcode_config
{
  video_codec input_codec{video_codec::h264};   // Detected from input
  video_codec output_codec{video_codec::h264};  // Desired output codec
  encoder_type encoder{encoder_type::software};
  uint32_t bitrate_kbps{5000};                  // Target bitrate in kbps
  uint32_t max_bitrate_kbps{8000};              // Maximum bitrate for VBR
  uint32_t gop_size{30};                        // Group of pictures size
  std::string preset{"medium"};                 // Encoder preset (quality vs speed)
  std::string profile{"main"};                  // Codec profile
  bool enable_scaling{false};                   // Enable resolution scaling
  uint32_t output_width{1920};                  // Scaled width (if enabled)
  uint32_t output_height{1080};                 // Scaled height (if enabled)
  uint32_t fps_num{30};                         // Output framerate numerator
  uint32_t fps_den{1};                          // Output framerate denominator
};

// Output destination configuration
export struct output_destination_config
{
  std::string id;                              // Unique identifier
  std::string name;                            // Human-readable name
  output_protocol protocol{output_protocol::none};
  std::string address;                         // Destination address/URL
  uint16_t port{0};                            // Destination port
  video_codec codec{video_codec::h264};        // Output codec (can differ per destination)
  encoder_type encoder{encoder_type::software}; // Encoder for this output
  uint32_t bitrate_kbps{5000};                 // Bitrate for this output
  bool enabled{true};                          // Enable/disable this output
  uint32_t reconnect_delay_ms{5000};           // Reconnection delay
  uint32_t max_reconnect_attempts{10};         // Max reconnection attempts
  
  // Protocol-specific settings
  union {
    struct {
      uint32_t buffer_min;
      uint32_t buffer_max;
      uint32_t rtt_min;
      uint32_t rtt_max;
      uint32_t reorder_buffer;
    } rist_settings;
    
    struct {
      uint32_t latency_ms;
      uint32_t overhead_bandwidth;
      std::string passphrase;
    } srt_settings;
    
    struct {
      std::string stream_key;
      std::string application;
    } rtmp_settings;
    
    struct {
      uint8_t payload_type;
      uint32_t ssrc;
      std::string session_name;
    } sdp_settings;
  };
};

// Runtime statistics
export struct output_stats
{
  std::string destination_id;
  output_state state{output_state::idle};
  uint64_t bytes_sent{0};
  uint64_t packets_sent{0};
  uint64_t packets_dropped{0};
  uint64_t reconnect_count{0};
  uint32_t current_bitrate_kbps{0};
  double link_quality{100.0};        // For RIST: 0-100%
  std::chrono::steady_clock::time_point last_activity;
  std::string error_message;
};

export struct transcoder_stats
{
  uint64_t frames_received{0};
  uint64_t frames_encoded{0};
  uint64_t frames_dropped{0};
  double current_fps{0.0};
  uint32_t current_bitrate_kbps{0};
  std::chrono::milliseconds encoding_latency{0};
  std::string pipeline_state;
};

export struct receiver_stats
{
  uint64_t bytes_received{0};
  uint64_t packets_received{0};
  uint64_t packets_lost{0};
  uint64_t packets_retransmitted{0};
  double link_quality{100.0};
  uint32_t current_bitrate_kbps{0};
  std::chrono::milliseconds network_latency{0};
};

// Global application state
export struct library
{
  library() noexcept;
  
  std::atomic_bool is_running{false};
  std::atomic_bool is_transcoding{false};
  
  // Configuration
  rist_input_config input_config;
  transcode_config transcode_settings;
  std::vector<output_destination_config> output_configs;
  
  // Runtime statistics
  receiver_stats recv_stats;
  transcoder_stats trans_stats;
  std::vector<output_stats> output_stats_list;
  
  // Threading
  std::vector<std::thread> threads;
  
  // Logging callback
  using log_func_ptr = void (*)(const std::string& msg, const std::string& component);
  log_func_ptr log_callback{nullptr};
  
  void log(const std::string& msg, const std::string& component = "main") const;
};
```

---

### 3.2 [`receiver`](source/receiver/receiver.cppm) Module

**File**: `source/receiver/receiver.cppm`  
**Exports**: `receiver`

**Purpose**: Manages RIST stream reception using the rist-cpp library.

```cpp
export class receiver
{
public:
  using data_received_callback = std::function<void(const uint8_t* data, size_t size, 
                                                     uint64_t pts, uint64_t dts)>;
  using connection_callback = std::function<void(const std::string& peer_address, bool connected)>;
  using stats_callback = std::function<void(const receiver_stats& stats)>;
  using log_callback = std::function<void(const std::string& msg)>;

  explicit receiver(const rist_input_config& config);
  ~receiver();

  // Non-copyable, non-movable
  receiver(const receiver&) = delete;
  receiver& operator=(const receiver&) = delete;
  receiver(receiver&&) = delete;
  receiver& operator=(receiver&&) = delete;

  // Lifecycle
  bool initialize();
  void start();
  void stop();
  bool is_running() const { return m_running; }

  // Callback registration
  void set_data_received_callback(data_received_callback cb);
  void set_connection_callback(connection_callback cb);
  void set_stats_callback(stats_callback cb);
  void set_log_callback(log_callback cb);

  // Statistics
  receiver_stats get_stats() const;
  
  // Connection management
  size_t get_connected_peer_count() const;
  void disconnect_peer(const std::string& peer_address);

private:
  const rist_input_config& m_config;
  std::unique_ptr<RISTNetReceiver> m_rist_receiver;
  
  // Callbacks
  data_received_callback m_data_cb{nullptr};
  connection_callback m_conn_cb{nullptr};
  stats_callback m_stats_cb{nullptr};
  log_callback m_log_cb{nullptr};
  
  // State
  std::atomic<bool> m_running{false};
  mutable std::mutex m_stats_mutex;
  receiver_stats m_stats;
  
  // RIST callback handlers
  int handle_received_data(const uint8_t* buf, size_t size, 
                           std::shared_ptr<RISTNetReceiver::NetworkConnection>& conn,
                           rist_peer* peer, uint16_t connection_id);
  std::shared_ptr<RISTNetReceiver::NetworkConnection> handle_validate_connection(
      std::string ip, uint16_t port);
  void handle_client_disconnected(const std::shared_ptr<RISTNetReceiver::NetworkConnection>& conn,
                                  const rist_peer& peer);
  void handle_statistics(const rist_stats& stats);
  
  // Internal methods
  void update_stats(const rist_stats& stats);
  void log(const std::string& msg);
};
```

---

### 3.3 [`transcoder`](source/transcoder/transcoder.cppm) Module

**File**: `source/transcoder/transcoder.cppm`  
**Exports**: `transcoder`

**Purpose**: GStreamer-based transcoding pipeline that converts input streams to target formats.

```cpp
export class transcoder
{
public:
  using frame_ready_callback = std::function<void(const shared_buffer& buffer)>;
  using error_callback = std::function<void(const std::string& error)>;
  using stats_callback = std::function<void(const transcoder_stats& stats)>;
  using log_callback = std::function<void(const std::string& msg)>;

  explicit transcoder(const transcode_config& config);
  ~transcoder();

  // Non-copyable, non-movable
  transcoder(const transcoder&) = delete;
  transcoder& operator=(const transcoder&) = delete;

  // Lifecycle
  bool initialize();
  void start();
  void stop();
  bool is_running() const { return m_running; }

  // Data input (from receiver)
  bool push_data(const uint8_t* data, size_t size, uint64_t pts, uint64_t dts);

  // Callback registration
  void set_frame_ready_callback(frame_ready_callback cb);
  void set_error_callback(error_callback cb);
  void set_stats_callback(stats_callback cb);
  void set_log_callback(log_callback cb);

  // Dynamic reconfiguration
  void update_bitrate(uint32_t new_bitrate_kbps);
  void update_encoder_preset(const std::string& preset);

  // Statistics
  transcoder_stats get_stats() const;

private:
  const transcode_config& m_config;
  
  // GStreamer pipeline elements
  GstElement* m_pipeline{nullptr};
  GstElement* m_appsrc{nullptr};           // Input from receiver
  GstElement* m_tsdemux{nullptr};          // MPEG-TS demuxer
  GstElement* m_decodebin{nullptr};        // Video decoder
  GstElement* m_videoconvert{nullptr};     // Format converter
  GstElement* m_video_encoder{nullptr};    // Hardware/software encoder
  GstElement* m_parser{nullptr};           // Video parser (h264parse/h265parse/av1parse)
  GstElement* m_muxer{nullptr};            // MPEG-TS muxer
  GstElement* m_appsink{nullptr};          // Output to fanout
  GstBus* m_bus{nullptr};
  
  // Callbacks
  frame_ready_callback m_frame_cb{nullptr};
  error_callback m_error_cb{nullptr};
  stats_callback m_stats_cb{nullptr};
  log_callback m_log_cb{nullptr};
  
  // State
  std::atomic<bool> m_running{false};
  mutable std::mutex m_stats_mutex;
  transcoder_stats m_stats;
  
  // Pipeline building
  bool build_pipeline();
  void destroy_pipeline();
  bool create_encoder_element();
  bool link_elements();
  
  // GStreamer callbacks (static with user_data)
  static GstFlowReturn on_new_sample(GstElement* sink, transcoder* self);
  static gboolean on_bus_message(GstBus* bus, GstMessage* msg, transcoder* self);
  
  // Internal handlers
  void handle_buffer(GstSample* sample);
  void handle_gst_error(GstMessage* msg);
  void handle_gst_eos();
  void update_stats_periodically();
  void log(const std::string& msg);
};
```

#### Transcoding Pipeline String

```
appsrc name=input_src ! 
  tsparse set-timestamps=true ! 
  tsdemux name=demux 

// Video path
demux.video_0 ! queue ! decodebin3 ! videoconvert ! 
  {encoder_element} ! {parser_element} ! 
  mpegtsmux name=mux ! appsink name=output_sink

// Audio path (passthrough or transcode)
demux.audio_0 ! queue ! decodebin3 ! audioconvert ! 
  avenc_aac ! aacparse ! mux.
```

**Encoder Elements Mapping**:

| Codec | NVIDIA | Intel QSV | AMD AMF | Software |
|-------|--------|-----------|---------|----------|
| H.264 | `nvh264enc` | `qsvh264enc` | `amfh264enc` | `x264enc` |
| H.265 | `nvh265enc` | `qsvh265enc` | `amfh265enc` | `x265enc` |
| AV1 | `nvav1enc` | `qsvav1enc` | N/A | `rav1enc` |

---

### 3.4 [`fanout`](source/fanout/fanout.cppm) Module

**File**: `source/fanout/fanout.cppm`  
**Exports**: `fanout`

**Purpose**: Distributes transcoded buffers to multiple output handlers using reference counting for zero-copy operation.

```cpp
export class buffer_fanout
{
public:
  using output_handler = std::function<void(const shared_buffer& buffer)>;
  using drop_callback = std::function<void(const std::string& output_id, uint64_t dropped_count)>;

  explicit fanout(size_t max_queue_size = 100);
  ~fanout();

  // Non-copyable, non-movable
  fanout(const fanout&) = delete;
  fanout& operator=(const fanout&) = delete;

  // Output registration
  void register_output(const std::string& output_id, output_handler handler);
  void unregister_output(const std::string& output_id);
  
  // Enable/disable outputs without unregistering
  void enable_output(const std::string& output_id, bool enabled);
  bool is_output_enabled(const std::string& output_id) const;

  // Data distribution
  void distribute(const shared_buffer& buffer);
  
  // Buffer management
  void set_max_queue_size(const std::string& output_id, size_t max_size);
  size_t get_queue_size(const std::string& output_id) const;
  void clear_all_queues();

  // Callback registration
  void set_drop_callback(drop_callback cb);

  // Statistics
  struct fanout_stats
  {
    uint64_t total_buffers_received{0};
    uint64_t total_buffers_distributed{0};
    uint64_t total_buffers_dropped{0};
    std::map<std::string, uint64_t> per_output_dropped;
  };
  fanout_stats get_stats() const;

private:
  struct output_entry
  {
    std::string id;
    output_handler handler;
    std::atomic<bool> enabled{true};
    std::queue<shared_buffer> queue;
    mutable std::mutex queue_mutex;
    std::condition_variable queue_cv;
    size_t max_queue_size{100};
    std::thread worker_thread;
    std::atomic<bool> should_exit{false};
    uint64_t dropped_count{0};
  };

  std::unordered_map<std::string, std::unique_ptr<output_entry>> m_outputs;
  mutable std::shared_mutex m_outputs_mutex;
  
  drop_callback m_drop_cb{nullptr};
  mutable std::mutex m_stats_mutex;
  fanout_stats m_stats;
  
  // Worker thread function for each output
  void output_worker(output_entry* entry);
  
  // Internal methods
  void update_stats(const std::string& output_id, bool dropped);
};
```

---

### 3.5 [`output_base`](source/output/output_base.cppm) Module

**File**: `source/output/output_base.cppm`  
**Exports**: `output_base`

**Purpose**: Abstract base class for all output protocol implementations.

```cpp
export class output_handler
{
public:
  using state_change_callback = std::function<void(const std::string& output_id, output_state new_state)>;
  using error_callback = std::function<void(const std::string& output_id, const std::string& error)>;
  using log_callback = std::function<void(const std::string& output_id, const std::string& msg)>;

  explicit output_handler(const output_destination_config& config);
  virtual ~output_handler() = default;

  // Non-copyable, non-movable
  output_handler(const output_handler&) = delete;
  output_handler& operator=(const output_handler&) = delete;
  output_handler(output_handler&&) = delete;
  output_handler& operator=(output_handler&&) = delete;

  // Core interface
  virtual bool initialize() = 0;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual bool is_connected() const = 0;
  virtual void send_buffer(const shared_buffer& buffer) = 0;
  
  // State management
  output_state get_state() const { return m_state; }
  const std::string& get_id() const { return m_config.id; }
  const std::string& get_name() const { return m_config.name; }
  output_protocol get_protocol() const { return m_config.protocol; }
  
  // Callback registration
  void set_state_change_callback(state_change_callback cb);
  void set_error_callback(error_callback cb);
  void set_log_callback(log_callback cb);
  
  // Statistics
  virtual output_stats get_stats() const;

protected:
  const output_destination_config& m_config;
  std::atomic<output_state> m_state{output_state::idle};
  mutable std::mutex m_stats_mutex;
  output_stats m_stats;
  
  // Callbacks
  state_change_callback m_state_cb{nullptr};
  error_callback m_error_cb{nullptr};
  log_callback m_log_cb{nullptr};
  
  // Helper methods
  void set_state(output_state new_state);
  void report_error(const std::string& error);
  void log(const std::string& msg);
  void update_stats(std::function<void(output_stats&)> updater);
};

// Factory function for creating output handlers
export std::unique_ptr<output_handler> create_output_handler(const output_destination_config& config);
```

---

### 3.6 [`output_rist`](source/output/output_rist.cppm) Module

**File**: `source/output/output_rist.cppm`  
**Exports**: `output_rist`

**Purpose**: RIST protocol output using rist-cpp RISTNetSender.

```cpp
export class rist_output : public output_handler
{
public:
  explicit rist_output(const output_destination_config& config);
  ~rist_output() override;

  // Implementation of output_handler interface
  bool initialize() override;
  void start() override;
  void stop() override;
  bool is_connected() const override;
  void send_buffer(const shared_buffer& buffer) override;
  output_stats get_stats() const override;

private:
  std::unique_ptr<RISTNetSender> m_sender;
  
  // RIST callbacks
  void handle_statistics(const rist_stats& stats);
  
  // Connection management
  bool setup_sender();
  void teardown_sender();
  
  // Internal state
  std::atomic<bool> m_connected{false};
  mutable std::mutex m_rist_mutex;
};
```

---

### 3.7 [`output_srt`](source/output/output_srt.cppm) Module

**File**: `source/output/output_srt.cppm`  
**Exports**: `output_srt`

**Purpose**: SRT protocol output using GStreamer srtsink element.

```cpp
export class srt_output : public output_handler
{
public:
  explicit srt_output(const output_destination_config& config);
  ~srt_output() override;

  // Implementation of output_handler interface
  bool initialize() override;
  void start() override;
  void stop() override;
  bool is_connected() const override;
  void send_buffer(const shared_buffer& buffer) override;

private:
  // GStreamer pipeline
  GstElement* m_pipeline{nullptr};
  GstElement* m_appsrc{nullptr};
  GstElement* m_srtsink{nullptr};
  GstBus* m_bus{nullptr};
  
  // Connection monitoring
  std::thread m_watchdog_thread;
  std::atomic<bool> m_watchdog_running{false};
  
  // Internal methods
  bool build_pipeline();
  void destroy_pipeline();
  void watchdog_loop();
  static gboolean on_bus_message(GstBus* bus, GstMessage* msg, srt_output* self);
};
```

**Pipeline String**:
```
appsrc name=input_src ! tsparse ! tsdemux name=demux

demux.video_0 ! queue ! decodebin3 ! videoconvert ! {encoder} ! {parser} ! mux.
demux.audio_0 ! queue ! decodebin3 ! audioconvert ! avenc_aac ! aacparse ! mux.

mpegtsmux name=mux ! srtsink uri="srt://{host}:{port}?latency={latency}" 
  wait-for-connection=false
```

---

### 3.8 [`output_rtmp`](source/output/output_rtmp.cppm) Module

**File**: `source/output/output_rtmp.cppm`  
**Exports**: `output_rtmp`

**Purpose**: RTMP protocol output using GStreamer rtmpsink element.

```cpp
export class rtmp_output : public output_handler
{
public:
  explicit rtmp_output(const output_destination_config& config);
  ~rtmp_output() override;

  // Implementation of output_handler interface
  bool initialize() override;
  void start() override;
  void stop() override;
  bool is_connected() const override;
  void send_buffer(const shared_buffer& buffer) override;

private:
  // GStreamer pipeline
  GstElement* m_pipeline{nullptr};
  GstElement* m_appsrc{nullptr};
  GstElement* m_flvmux{nullptr};
  GstElement* m_rtmpsink{nullptr};
  GstBus* m_bus{nullptr};
  
  // Internal methods
  bool build_pipeline();
  void destroy_pipeline();
  static gboolean on_bus_message(GstBus* bus, GstMessage* msg, rtmp_output* self);
  
  // URL construction
  std::string build_rtmp_url() const;
};
```

**Pipeline String**:
```
appsrc name=input_src ! tsparse ! tsdemux name=demux

demux.video_0 ! queue ! decodebin3 ! videoconvert ! {encoder} ! {parser} ! flvmux name=mux.
demux.audio_0 ! queue ! decodebin3 ! audioconvert ! avenc_aac ! aacparse ! mux.

mux. ! rtmpsink location="{rtmp_url}"
```

---

### 3.9 [`output_sdp`](source/output/output_sdp.cppm) Module

**File**: `source/output/output_sdp.cppm`  
**Exports**: `output_sdp`

**Purpose**: SDP/RTP output using GStreamer udpsink with RTP payloaders.

```cpp
export class sdp_output : public output_handler
{
public:
  explicit sdp_output(const output_destination_config& config);
  ~sdp_output() override;

  // Implementation of output_handler interface
  bool initialize() override;
  void start() override;
  void stop() override;
  bool is_connected() const override;
  void send_buffer(const shared_buffer& buffer) override;
  
  // SDP generation
  std::string generate_sdp() const;
  bool write_sdp_to_file(const std::string& filepath) const;

private:
  // GStreamer pipeline
  GstElement* m_pipeline{nullptr};
  GstElement* m_appsrc{nullptr};
  GstElement* m_rtp_payloader{nullptr};
  GstElement* m_udpsink{nullptr};
  GstBus* m_bus{nullptr};
  
  // Internal methods
  bool build_pipeline();
  void destroy_pipeline();
  static gboolean on_bus_message(GstBus* bus, GstMessage* msg, sdp_output* self);
  
  // RTP payloader selection
  GstElement* create_payloader(video_codec codec);
};
```

**Pipeline String**:
```
appsrc name=input_src ! tsparse ! tsdemux name=demux

demux.video_0 ! queue ! decodebin3 ! videoconvert ! {encoder} ! {parser} ! 
  {rtp_payloader} pt={payload_type} ssrc={ssrc} ! udpsink host={host} port={port}

demux.audio_0 ! queue ! decodebin3 ! audioconvert ! avenc_aac ! aacparse ! 
  rtpmp4apay pt=97 ! udpsink host={host} port={port+2}
```

---

### 3.10 [`config`](source/config/config.cppm) Module

**File**: `source/config/config.cppm`  
**Exports**: `config`

**Purpose**: JSON configuration file management.

```cpp
export class configuration_manager
{
public:
  using validation_error = std::pair<std::string, std::string>; // field, message

  explicit configuration_manager(const std::string& config_path);
  
  // Load/Save
  bool load();
  bool save();
  bool load_from_string(const std::string& json_str);
  std::string save_to_string() const;
  
  // Validation
  bool validate();
  std::vector<validation_error> get_validation_errors() const;
  
  // Configuration access
  rist_input_config& get_input_config();
  transcode_config& get_transcode_config();
  std::vector<output_destination_config>& get_output_configs();
  
  // Output management
  void add_output(const output_destination_config& config);
  void remove_output(const std::string& output_id);
  output_destination_config* get_output(const std::string& output_id);
  
  // Defaults
  static rist_input_config get_default_input_config();
  static transcode_config get_default_transcode_config();
  static output_destination_config get_default_output_config(output_protocol protocol);

private:
  std::string m_config_path;
  nlohmann::json m_json;
  
  rist_input_config m_input_config;
  transcode_config m_transcode_config;
  std::vector<output_destination_config> m_output_configs;
  std::vector<validation_error> m_validation_errors;
  
  // Serialization helpers
  nlohmann::json input_config_to_json(const rist_input_config& cfg) const;
  nlohmann::json transcode_config_to_json(const transcode_config& cfg) const;
  nlohmann::json output_config_to_json(const output_destination_config& cfg) const;
  
  rist_input_config json_to_input_config(const nlohmann::json& j) const;
  transcode_config json_to_transcode_config(const nlohmann::json& j) const;
  output_destination_config json_to_output_config(const nlohmann::json& j) const;
};
```

---

### 3.11 [`stats`](source/stats/stats.cppm) Module

**File**: `source/stats/stats.cppm`  
**Exports**: `stats`

**Purpose**: Statistics collection, aggregation, and UI updates.

```cpp
export class statistics_manager
{
public:
  using ui_update_callback = std::function<void()>;

  explicit statistics_manager(library& lib);
  
  void start_collection(std::chrono::milliseconds interval = std::chrono::seconds(1));
  void stop_collection();
  
  // Update methods called by components
  void update_receiver_stats(const receiver_stats& stats);
  void update_transcoder_stats(const transcoder_stats& stats);
  void update_output_stats(const std::string& output_id, const output_stats& stats);
  
  // UI callback
  void set_ui_update_callback(ui_update_callback cb);
  
  // Accessors for UI
  const receiver_stats& get_receiver_stats() const;
  const transcoder_stats& get_transcoder_stats() const;
  const std::vector<output_stats>& get_all_output_stats() const;
  output_stats get_output_stats(const std::string& output_id) const;

private:
  library& m_lib;
  std::thread m_collection_thread;
  std::atomic<bool> m_running{false};
  std::chrono::milliseconds m_interval;
  ui_update_callback m_ui_cb{nullptr};
  
  mutable std::mutex m_stats_mutex;
  receiver_stats m_receiver_stats;
  transcoder_stats m_transcoder_stats;
  std::vector<output_stats> m_output_stats;
  
  void collection_loop();
};

// Utility functions for calculating moving averages
export double calculate_moving_average(const std::vector<double>& values);
export std::string format_bitrate(uint32_t kbps);
export std::string format_duration(std::chrono::milliseconds duration);
```

---

### 3.12 [`ui`](source/ui/ui.cppm) Module

**File**: `source/ui/ui.cppm`  
**Exports**: `ui`

**Purpose**: FLTK-based user interface.

```cpp
export class user_interface
{
public:
  user_interface();
  ~user_interface();

  // Initialization
  void init_ui();
  void init_ui_callbacks(library* lib,
                         std::function<void()> start_callback,
                         std::function<void()> stop_callback,
                         std::function<void()> add_output_callback,
                         std::function<void(const std::string&)> remove_output_callback,
                         std::function<void()> load_config_callback,
                         std::function<void()> save_config_callback);
  
  void show(int argc, char** argv);
  auto run_ui() -> int;
  
  // Threading helpers
  void lock();      // Calls Fl::lock()
  void unlock();    // Calls Fl::unlock(); Fl::awake();
  
  // Log appenders
  void log_append(const std::string& msg, const std::string& component = "general");
  void receiver_log_append(const std::string& msg);
  void transcoder_log_append(const std::string& msg);
  void output_log_append(const std::string& output_id, const std::string& msg);
  
  // Statistics updates (thread-safe, uses lock/unlock internally)
  void update_receiver_stats(const receiver_stats& stats);
  void update_transcoder_stats(const transcoder_stats& stats);
  void update_output_stats(const output_stats& stats);
  void update_output_state(const std::string& output_id, output_state state);
  
  // Output management UI
  void add_output_row(const output_destination_config& config);
  void remove_output_row(const std::string& output_id);
  void update_output_row(const output_destination_config& config);
  
  // Configuration UI
  void load_config_to_ui(const library& lib);
  void save_config_from_ui(library& lib);
  
  // Control state
  void set_running_state(bool running);
  void show_error_dialog(const std::string& title, const std::string& message);
  void show_info_dialog(const std::string& title, const std::string& message);

  // UI Components (public for Fluid bindings)
  Fl_Double_Window* main_window;
  
  // Input configuration
  Fl_Input* input_listen_address;
  Fl_Input* input_listen_port;
  Fl_Input* input_buffer_min;
  Fl_Input* input_buffer_max;
  Fl_Input* input_rtt_min;
  Fl_Input* input_rtt_max;
  Fl_Input* input_reorder_buffer;
  Fl_Button* btn_apply_input_config;
  
  // Transcoding configuration
  Fl_Choice* choice_output_codec;
  static Fl_Menu_Item menu_choice_output_codec[];
  Fl_Choice* choice_encoder;
  static Fl_Menu_Item menu_choice_encoder[];
  Fl_Input* input_bitrate;
  Fl_Input* input_max_bitrate;
  Fl_Choice* choice_preset;
  static Fl_Menu_Item menu_choice_preset[];
  Fl_Button* btn_apply_transcode_config;
  
  // Output destinations
  Fl_Scroll* outputs_scroll;
  Fl_Flex* outputs_list;
  Fl_Button* btn_add_output;
  
  // Statistics display
  Fl_Output* recv_bytes_output;
  Fl_Output* recv_packets_output;
  Fl_Output* recv_quality_output;
  Fl_Output* trans_fps_output;
  Fl_Output* trans_bitrate_output;
  Fl_Output* trans_latency_output;
  Fl_Grid* outputs_stats_grid;
  
  // Control buttons
  Fl_Button* btn_start;
  Fl_Button* btn_stop;
  Fl_Button* btn_load_config;
  Fl_Button* btn_save_config;
  
  // Log displays
  Fl_Text_Display* receiver_log_display;
  Fl_Text_Display* transcoder_log_display;
  Fl_Text_Display* outputs_log_display;
  Fl_Text_Buffer receiver_log_buffer;
  Fl_Text_Buffer transcoder_log_buffer;
  Fl_Text_Buffer outputs_log_buffer;

private:
  library* m_lib{nullptr};
  
  // Callback storage
  std::function<void()> m_start_cb;
  std::function<void()> m_stop_cb;
  std::function<void()> m_add_output_cb;
  std::function<void(const std::string&)> m_remove_output_cb;
  std::function<void()> m_load_config_cb;
  std::function<void()> m_save_config_cb;
  
  // Internal UI helpers
  void build_output_row(const output_destination_config& config);
  void remove_output_row_internal(const std::string& output_id);
};
```

---

## 4. Data Flow Architecture

### 4.1 End-to-End Flow

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              DATA FLOW PIPELINE                                  │
└─────────────────────────────────────────────────────────────────────────────────┘

  RIST Sender          Receiver          Transcoder         Fanout          Outputs
      │                  │                  │                │                │
      │  RTP/TS          │                  │                │                │
      │══════════════════▶│                  │                │                │
      │                  │  Raw TS Buffer   │                │                │
      │                  │──────────────────▶│                │                │
      │                  │                  │                │                │
      │                  │                  │ Decode/Encode  │                │
      │                  │                  │────┐           │                │
      │                  │                  │    │           │                │
      │                  │                  │◀───┘           │                │
      │                  │                  │                │                │
      │                  │                  │ shared_buffer  │                │
      │                  │                  │────────────────▶│                │
      │                  │                  │                │                │
      │                  │                  │                │ Reference      │
      │                  │                  │                │ Count +1       │
      │                  │                  │                │ for each       │
      │                  │                  │                │ output         │
      │                  │                  │                │                │
      │                  │                  │                │ ┌────────────┐ │
      │                  │                  │                │ │ Output 1   │ │
      │                  │                  │                │ │ (RIST)     │◀┘
      │                  │                  │                │ └────────────┘
      │                  │                  │                │ ┌────────────┐
      │                  │                  │                │ │ Output 2   │
      │                  │                  │                │ │ (SRT)      │◀──┐
      │                  │                  │                │ └────────────┘   │
      │                  │                  │                │ ┌────────────┐   │
      │                  │                  │                │ │ Output 3   │   │
      │                  │                  │                │ │ (RTMP)     │◀──┤
      │                  │                  │                │ └────────────┘   │
      │                  │                  │                │      ...         │
      │                  │                  │                │                  │
      │                  │                  │                │ After send:      │
      │                  │                  │                │ Ref Count--      │
      │                  │                  │                │ Free if 0        │
```

### 4.2 Buffer Lifecycle

```
┌─────────────────────────────────────────────────────────────────────┐
│                     SHARED_BUFFER LIFECYCLE                          │
└─────────────────────────────────────────────────────────────────────┘

  Creation
     │
     ▼
┌─────────┐     ┌──────────────────────────────────────┐
│ Receiver│────▶│ shared_buffer                        │
│ creates │     │ {                                    │
│ buffer  │     │   data: shared_ptr<vector<uint8_t>>,│
└─────────┘     │   ref_count: 1,                      │
                │   timestamp: pts/dts                 │
                │ }                                    │
                └──────────────────────────────────────┘
                              │
                              │ push to transcoder
                              ▼
                ┌──────────────────────────────────────┐
                │ Transcoder processes, creates new    │
                │ shared_buffer with encoded frame     │
                │ ref_count: 1                         │
                └──────────────────────────────────────┘
                              │
                              │ distribute to fanout
                              ▼
                ┌──────────────────────────────────────┐
                │ Fanout increments ref_count for      │
                │ each registered output:              │
                │ ref_count = 1 + N (outputs)          │
                └──────────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
    ┌──────────┐       ┌──────────┐       ┌──────────┐
    │ Output 1 │       │ Output 2 │       │ Output N │
    │ (sends)  │       │ (sends)  │       │ (sends)  │
    │ ref--    │       │ ref--    │       │ ref--    │
    └──────────┘       └──────────┘       └──────────┘
          │                   │                   │
          └───────────────────┼───────────────────┘
                              ▼
                ┌──────────────────────────────────────┐
                │ When ref_count reaches 0:            │
                │ shared_ptr destructor frees memory   │
                └──────────────────────────────────────┘
```

---

## 5. Threading Model

### 5.1 Thread Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           THREAD ARCHITECTURE                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                         MAIN THREAD                                  │   │
│  │  • UI event loop (Fl::run())                                        │   │
│  │  • Configuration management                                         │   │
│  │  • Component lifecycle orchestration                                │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                    │                                        │
│                    ┌───────────────┼───────────────┐                       │
│                    ▼               ▼               ▼                       │
│  ┌─────────────────────┐ ┌─────────────────┐ ┌─────────────────────┐       │
│  │   RECEIVER THREAD    │ │ TRANSCODER      │ │  STATS COLLECTION   │       │
│  │  (librist internal)  │ │ WORKER THREAD(S)│ │     THREAD          │       │
│  │                      │ │                 │ │                     │       │
│  │ • RISTNetReceiver    │ │ • GStreamer     │ │ • Periodic updates  │       │
│  │   network I/O        │ │   pipeline      │ │ • UI refresh        │       │
│  │ • Data callback      │ │ • Buffer pull   │ │   (via Fl::awake)   │       │
│  │   (transcoder push)  │ │ • Frame push    │ │                     │       │
│  └─────────────────────┘ └─────────────────┘ └─────────────────────┘       │
│                                                  │                          │
│  ┌───────────────────────────────────────────────┼─────────────────────┐   │
│  │                    OUTPUT THREADS              │                     │   │
│  │                                               ▼                     │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │   │
│  │  │ Output 1    │  │ Output 2    │  │ Output 3    │  │ Output N    │ │   │
│  │  │ (RIST)      │  │ (SRT)       │  │ (RTMP)      │  │ (...)       │ │   │
│  │  │             │  │             │  │             │  │             │ │   │
│  │  │ • rist-cpp  │  │ • GStreamer │  │ • GStreamer │  │ • Protocol  │ │   │
│  │  │   sender    │  │   pipeline  │  │   pipeline  │  │   handler   │ │   │
│  │  │ • Network   │  │ • srt/udp   │  │ • rtmp send │  │             │ │   │
│  │  │   send      │  │   send      │  │             │  │             │ │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘ │   │
│  │                                                                     │   │
│  │  Each output runs in its own thread via fanout worker.             │   │
│  │  Independent reconnection logic per output.                        │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 5.2 Thread Synchronization

| Component | Thread | Synchronization Mechanism |
|-----------|--------|---------------------------|
| Receiver → Transcoder | Receiver internal → Transcoder worker | `transcoder::push_data()` with internal GStreamer `appsrc` queue |
| Transcoder → Fanout | Transcoder worker → Fanout distribute | `fanout::distribute()` with `shared_ptr` (thread-safe ref counting) |
| Fanout → Outputs | Fanout internal → Output workers | Per-output `std::queue` + `std::mutex` + `std::condition_variable` |
| Stats → UI | Stats thread → Main thread | `Fl::awake()` for UI updates, `Fl::lock()/unlock()` for UI access |
| UI → Components | Main thread → All | `std::atomic<bool>` flags, `std::mutex` for config access |

### 5.3 Callback Safety

```cpp
// Thread-safe callback invocation pattern
void component::safe_invoke_callback(std::function<void(Args...)> cb, Args... args)
{
  if (cb) {
    // For UI callbacks, use Fl::awake to marshal to main thread
    if (m_is_ui_callback) {
      Fl::awake([=]() { cb(args...); });
    } else {
      cb(args...);
    }
  }
}
```

---

## 6. Configuration Schema

### 6.1 JSON Schema

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "RIST Transcoder Configuration",
  "type": "object",
  "required": ["version", "input", "transcode", "outputs"],
  "properties": {
    "version": {
      "type": "string",
      "description": "Configuration schema version",
      "default": "1.0.0"
    },
    "input": {
      "type": "object",
      "required": ["listen_port"],
      "properties": {
        "listen_address": {
          "type": "string",
          "default": "0.0.0.0"
        },
        "listen_port": {
          "type": "integer",
          "minimum": 1,
          "maximum": 65535,
          "default": 5000
        },
        "buffer_min": {
          "type": "integer",
          "minimum": 0,
          "default": 245
        },
        "buffer_max": {
          "type": "integer",
          "minimum": 0,
          "default": 5000
        },
        "rtt_min": {
          "type": "integer",
          "minimum": 0,
          "default": 40
        },
        "rtt_max": {
          "type": "integer",
          "minimum": 0,
          "default": 500
        },
        "reorder_buffer": {
          "type": "integer",
          "minimum": 0,
          "default": 240
        },
        "session_timeout": {
          "type": "integer",
          "minimum": 1000,
          "default": 5000
        },
        "psk": {
          "type": "string",
          "description": "Pre-shared key for RIST encryption (optional)"
        },
        "cname": {
          "type": "string",
          "description": "CNAME for RIST session identification"
        }
      }
    },
    "transcode": {
      "type": "object",
      "properties": {
        "output_codec": {
          "type": "string",
          "enum": ["h264", "h265", "av1", "copy"],
          "default": "h264"
        },
        "encoder": {
          "type": "string",
          "enum": ["nvidia", "intel", "amd", "software"],
          "default": "software"
        },
        "bitrate_kbps": {
          "type": "integer",
          "minimum": 100,
          "default": 5000
        },
        "max_bitrate_kbps": {
          "type": "integer",
          "minimum": 100,
          "default": 8000
        },
        "gop_size": {
          "type": "integer",
          "minimum": 1,
          "default": 30
        },
        "preset": {
          "type": "string",
          "enum": ["ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"],
          "default": "medium"
        },
        "profile": {
          "type": "string",
          "default": "main"
        },
        "enable_scaling": {
          "type": "boolean",
          "default": false
        },
        "output_width": {
          "type": "integer",
          "minimum": 64,
          "default": 1920
        },
        "output_height": {
          "type": "integer",
          "minimum": 64,
          "default": 1080
        },
        "fps_num": {
          "type": "integer",
          "minimum": 1,
          "default": 30
        },
        "fps_den": {
          "type": "integer",
          "minimum": 1,
          "default": 1
        }
      }
    },
    "outputs": {
      "type": "array",
      "minItems": 0,
      "maxItems": 32,
      "items": {
        "type": "object",
        "required": ["id", "name", "protocol", "address"],
        "properties": {
          "id": {
            "type": "string",
            "pattern": "^[a-zA-Z0-9_-]+$"
          },
          "name": {
            "type": "string"
          },
          "enabled": {
            "type": "boolean",
            "default": true
          },
          "protocol": {
            "type": "string",
            "enum": ["rist", "srt", "rtmp", "sdp"]
          },
          "address": {
            "type": "string"
          },
          "port": {
            "type": "integer",
            "minimum": 1,
            "maximum": 65535
          },
          "codec": {
            "type": "string",
            "enum": ["h264", "h265", "av1"],
            "default": "h264"
          },
          "encoder": {
            "type": "string",
            "enum": ["nvidia", "intel", "amd", "software"],
            "default": "software"
          },
          "bitrate_kbps": {
            "type": "integer",
            "minimum": 100,
            "default": 5000
          },
          "reconnect_delay_ms": {
            "type": "integer",
            "minimum": 1000,
            "default": 5000
          },
          "max_reconnect_attempts": {
            "type": "integer",
            "minimum": 0,
            "default": 10
          },
          "rist_settings": {
            "type": "object",
            "properties": {
              "buffer_min": {"type": "integer", "default": 245},
              "buffer_max": {"type": "integer", "default": 5000},
              "rtt_min": {"type": "integer", "default": 40},
              "rtt_max": {"type": "integer", "default": 500},
              "reorder_buffer": {"type": "integer", "default": 240}
            }
          },
          "srt_settings": {
            "type": "object",
            "properties": {
              "latency_ms": {"type": "integer", "default": 120},
              "overhead_bandwidth": {"type": "integer", "default": 25},
              "passphrase": {"type": "string"}
            }
          },
          "rtmp_settings": {
            "type": "object",
            "properties": {
              "stream_key": {"type": "string"},
              "application": {"type": "string", "default": "live"}
            }
          },
          "sdp_settings": {
            "type": "object",
            "properties": {
              "payload_type": {"type": "integer", "minimum": 96, "maximum": 127, "default": 96},
              "ssrc": {"type": "integer"},
              "session_name": {"type": "string", "default": "RIST Transcoder Stream"}
            }
          }
        }
      }
    }
  }
}
```

### 6.2 Example Configuration

```json
{
  "version": "1.0.0",
  "input": {
    "listen_address": "0.0.0.0",
    "listen_port": 5000,
    "buffer_min": 245,
    "buffer_max": 5000,
    "rtt_min": 40,
    "rtt_max": 500,
    "reorder_buffer": 240,
    "session_timeout": 5000,
    "cname": "rist-transcoder-receiver"
  },
  "transcode": {
    "output_codec": "h264",
    "encoder": "nvidia",
    "bitrate_kbps": 6000,
    "max_bitrate_kbps": 8000,
    "gop_size": 30,
    "preset": "medium",
    "profile": "main",
    "enable_scaling": false,
    "output_width": 1920,
    "output_height": 1080,
    "fps_num": 30,
    "fps_den": 1
  },
  "outputs": [
    {
      "id": "primary-rist",
      "name": "Primary RIST Output",
      "enabled": true,
      "protocol": "rist",
      "address": "192.168.1.100",
      "port": 6000,
      "codec": "h264",
      "encoder": "nvidia",
      "bitrate_kbps": 6000,
      "reconnect_delay_ms": 5000,
      "max_reconnect_attempts": 10,
      "rist_settings": {
        "buffer_min": 245,
        "buffer_max": 5000,
        "rtt_min": 40,
        "rtt_max": 500,
        "reorder_buffer": 240
      }
    },
    {
      "id": "backup-srt",
      "name": "Backup SRT Output",
      "enabled": true,
      "protocol": "srt",
      "address": "192.168.1.101",
      "port": 7000,
      "codec": "h264",
      "encoder": "software",
      "bitrate_kbps": 4000,
      "srt_settings": {
        "latency_ms": 120,
        "overhead_bandwidth": 25
      }
    },
    {
      "id": "youtube-rtmp",
      "name": "YouTube RTMP",
      "enabled": false,
      "protocol": "rtmp",
      "address": "a.rtmp.youtube.com",
      "port": 1935,
      "codec": "h264",
      "encoder": "software",
      "bitrate_kbps": 4500,
      "rtmp_settings": {
        "stream_key": "xxxx-xxxx-xxxx-xxxx",
        "application": "live2"
      }
    },
    {
      "id": "local-sdp",
      "name": "Local SDP/RTP",
      "enabled": true,
      "protocol": "sdp",
      "address": "239.255.1.1",
      "port": 5004,
      "codec": "h264",
      "sdp_settings": {
        "payload_type": 96,
        "ssrc": 12345678,
        "session_name": "RIST Transcoder Multicast"
      }
    }
  ]
}
```

---

## 7. UI Component Specifications

### 7.1 Main Window Layout

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  RIST Transcoder                                    [Minimize] [Max] [Close] │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─ Input Configuration ─────────────────────────────────────────────────┐  │
│  │  Listen Address: [0.0.0.0________]  Port: [5000____]                 │  │
│  │  Buffer (ms): Min [245_]  Max [5000_]                                │  │
│  │  RTT (ms):    Min [40__]  Max [500__]                                │  │
│  │  Reorder Buffer: [240_]  Session Timeout: [5000_]                     │  │
│  │                                                          [Apply]      │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─ Transcoding Configuration ───────────────────────────────────────────┐  │
│  │  Output Codec: [H.264 ▼]  Encoder: [NVIDIA NVENC ▼]                  │  │
│  │  Bitrate: [6000___] kbps  Max: [8000___] kbps                        │  │
│  │  Preset: [medium ▼]  Profile: [main ▼]  GOP: [30_]                   │  │
│  │  Scaling: [ ] Enable  Resolution: [1920_]x[1080_]                     │  │
│  │                                                          [Apply]      │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─ Output Destinations ─────────────────────────────────────────┐ [+ Add] │  │
│  │ ┌──────────────────────────────────────────────────────────┐ │          │
│  │ │ [Active] Primary RIST Output (RIST)           [Edit] [X] │ │          │
│  │ │    192.168.1.100:6000  |  H264/NVIDIA  |  Connected ✓    │ │          │
│  │ │    Bitrate: 5.8 Mbps | Quality: 98% | RTT: 12ms           │ │          │
│  │ ├──────────────────────────────────────────────────────────┤ │          │
│  │ │ [Active] Backup SRT Output (SRT)              [Edit] [X] │ │          │
│  │ │    192.168.1.101:7000  |  H264/Software  |  Connected ✓   │ │          │
│  │ │    Bitrate: 3.9 Mbps | Latency: 120ms                     │ │          │
│  │ ├──────────────────────────────────────────────────────────┤ │          │
│  │ │ [     ] YouTube RTMP (RTMP)                   [Edit] [X] │ │          │
│  │ │    a.rtmp.youtube.com:1935  |  H264/Software  |  Disabled │ │          │
│  │ └──────────────────────────────────────────────────────────┘ │          │
│  └────────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─ Statistics ──────────────────────────────────────────────────────────┐  │
│  │  Receiver:  12.5 Mbps | Packets: 1.2M | Quality: 98%                  │  │
│  │  Transcoder: 30 fps | Latency: 45ms | Bitrate: 6.0 Mbps               │  │
│  │  Outputs: 3 active | 0 errors | Total sent: 45.2 GB                   │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─ Logs ─────────────────────────────────────────────────────────────────┐  │
│  │  [Receiver] [Transcoder] [Outputs]                                     │  │
│  │  ┌────────────────────────────────────────────────────────────────┐    │  │
│  │  │ [2024-03-19 10:30:15] Receiver: Connection from 10.0.0.5:45678│    │  │
│  │  │ [2024-03-19 10:30:15] Transcoder: Pipeline started successfully │    │  │
│  │  │ [2024-03-19 10:30:16] Output primary-rist: Connected            │    │  │
│  │  │ ...                                                            │    │  │
│  │  └────────────────────────────────────────────────────────────────┘    │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  [   Start Transcoding   ]  [Load Config]  [Save Config]                    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 7.2 Output Configuration Dialog

```
┌─────────────────────────────────────────────┐
│  Edit Output Destination                    │
├─────────────────────────────────────────────┤
│                                             │
│  ID:          [primary-rist______________]  │
│  Name:        [Primary RIST Output_______]  │
│  Protocol:    [RIST ▼]                      │
│                                             │
│  Address:     [192.168.1.100_____________]  │
│  Port:        [6000______________________]  │
│                                             │
│  Codec:       [H.264 ▼]                     │
│  Encoder:     [NVIDIA NVENC ▼]              │
│  Bitrate:     [6000________] kbps           │
│                                             │
│  ┌─ RIST Settings ──────────────────────┐   │
│  │  Buffer Min:  [245_] ms              │   │
│  │  Buffer Max:  [5000] ms              │   │
│  │  RTT Min:     [40__] ms              │   │
│  │  RTT Max:     [500_] ms              │   │
│  │  Reorder:     [240_] ms              │   │
│  └──────────────────────────────────────┘   │
│                                             │
│  Reconnect Delay: [5000_] ms                │
│  Max Reconnects:  [10____]                  │
│                                             │
│              [   OK   ]  [ Cancel ]         │
│                                             │
└─────────────────────────────────────────────┘
```

---

## 8. Integration with open-broadcast-encoder

### 8.1 Protocol Compatibility

The RIST transcoder is designed to be a drop-in receiver for [`open-broadcast-encoder`](source/main.cpp) outputs:

| Parameter | open-broadcast-encoder | RIST Transcoder |
|-----------|----------------------|-----------------|
| Protocol | RIST (sender) | RIST (receiver) |
| Library | rist-cpp (RISTNetSender) | rist-cpp (RISTNetReceiver) |
| Default Port | 5000 | 5000 |
| Buffer Defaults | 245/5000 ms | 245/5000 ms |
| RTT Defaults | 40/500 ms | 40/500 ms |
| Encryption | PSK (optional) | PSK (optional) |
| Profile | RIST_PROFILE_ADVANCED | RIST_PROFILE_ADVANCED |

### 8.2 Connection Workflow

```
┌─────────────────────────┐                    ┌─────────────────────────┐
│  open-broadcast-encoder │                    │    RIST Transcoder      │
│  (RIST Sender)          │                    │  (RIST Receiver)        │
└─────────────────────────┘                    └─────────────────────────┘
           │                                              │
           │  1. User starts encoder                      │
           │  2. RISTNetSender.initSender()               │
           │     binds to source port                     │
           │                                              │
           │  3. Sender connects to transcoder address    │
           │══════════════════════════════════════════════▶│
           │     RIST handshake                           │
           │     validateConnectionCallback               │
           │                                              │
           │◀═════════════════════════════════════════════│
           │     Connection accepted                      │
           │                                              │
           │  4. MPEG-TS stream begins                    │
           │══════════════════════════════════════════════▶│
           │     networkDataCallback                      │
           │     push_data() → transcoder                 │
           │                                              │
           │  5. Continuous streaming...                  │
           │══════════════════════════════════════════════▶│
           │     Periodic statistics callbacks            │
           │                                              │
           │  6. User stops encoder or error              │
           │══════════════════════════════════════════════▶│
           │     clientDisconnectedCallback               │
           │     OR connection timeout                    │
```

### 8.3 URL Format Compatibility

**open-broadcast-encoder sends to**:
```
rist://{transcoder_ip}:{port}?bandwidth={bw}&buffer-min={min}&buffer-max={max}&
  rtt-min={rtt_min}&rtt-max={rtt_max}&reorder-buffer={reorder}&timing-mode=2
```

**RIST Transcoder listens on**:
```
rist://{listen_address}:{port}?buffer-min={min}&buffer-max={max}&
  rtt-min={rtt_min}&rtt-max={rtt_max}&reorder-buffer={reorder}
```

---

## 9. Error Handling Strategy

### 9.1 Error Categories

| Category | Examples | Handling Strategy |
|----------|----------|-------------------|
| **Network** | Connection lost, timeout, DNS failure | Automatic reconnection with exponential backoff |
| **Codec** | Decoder error, encoder error, unsupported format | Pipeline restart, fallback to software encoder |
| **Resource** | Out of memory, file descriptor exhaustion | Graceful degradation, drop non-critical outputs |
| **Configuration** | Invalid JSON, missing required fields | Validation at load time, detailed error messages |
| **Runtime** | Thread crash, GStreamer error | Component restart, log to file, UI notification |

### 9.2 Error Recovery Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          ERROR RECOVERY FLOW                                 │
└─────────────────────────────────────────────────────────────────────────────┘

  Error Detected
       │
       ▼
┌──────────────┐
│ Log Error    │──▶ Write to log file
│ (timestamped)│──▶ Update UI error display
└──────────────┘
       │
       ▼
┌──────────────┐     ┌─────────────┐
│ Classify     │────▶│ Network     │──▶ Reconnect with backoff
│ Error Type   │     │ Error       │     (max 10 attempts)
└──────────────┘     ├─────────────┤
       │             │ Codec Error │──▶ Restart pipeline,
       │             │             │     fallback encoder
       │             ├─────────────┤
       │             │ Resource    │──▶ Drop lowest priority
       │             │ Error       │     output, notify user
       │             ├─────────────┤
       │             │ Fatal Error │──▶ Stop all, show dialog
       └────────────▶│             │
                     └─────────────┘
```

### 9.3 Reconnection Logic (per output)

```cpp
class output_handler
{
  void handle_disconnect()
  {
    set_state(output_state::reconnecting);
    m_stats.reconnect_count++;
    
    int attempt = 0;
    while (attempt < m_config.max_reconnect_attempts && m_should_run)
    {
      // Exponential backoff: 1s, 2s, 4s, 8s, ... max 60s
      auto delay = std::min(1000 * (1 << attempt), 60000);
      
      log(std::format("Reconnecting in {}ms (attempt {}/{})...",
                      delay, attempt + 1, m_config.max_reconnect_attempts));
      
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      
      if (try_connect())
      {
        set_state(output_state::connected);
        log("Reconnection successful");
        return;
      }
      
      attempt++;
    }
    
    set_state(output_state::error);
    report_error(std::format("Max reconnection attempts ({}) exceeded",
                             m_config.max_reconnect_attempts));
  }
};
```

---

## 10. Buffer Lifecycle Management

### 10.1 Memory Allocation Strategy

```cpp
// Use shared_ptr for automatic reference counting
using buffer_data = std::vector<uint8_t>;
using shared_buffer_ptr = std::shared_ptr<buffer_data>;

struct shared_buffer
{
  shared_buffer_ptr data;
  uint64_t timestamp_pts{0};
  uint64_t timestamp_dts{0};
  bool is_keyframe{false};
  
  // Factory method for creating buffers
  static shared_buffer create(size_t size)
  {
    return shared_buffer{
      .data = std::make_shared<buffer_data>(size),
      .timestamp_pts = 0,
      .timestamp_dts = 0,
      .is_keyframe = false
    };
  }
  
  // Clone for per-output modifications (rarely needed)
  shared_buffer clone() const
  {
    auto new_buffer = create(data->size());
    std::copy(data->begin(), data->end(), new_buffer.data->begin());
    new_buffer.timestamp_pts = timestamp_pts;
    new_buffer.timestamp_dts = timestamp_dts;
    new_buffer.is_keyframe = is_keyframe;
    return new_buffer;
  }
};
```

### 10.2 Flow Control and Backpressure

```cpp
void fanout::distribute(const shared_buffer& buffer)
{
  std::shared_lock lock(m_outputs_mutex);
  
  for (auto& [id, entry] : m_outputs)
  {
    if (!entry->enabled)
      continue;
      
    std::unique_lock qlock(entry->queue_mutex);
    
    if (entry->queue.size() >= entry->max_queue_size)
    {
      // Queue full - drop oldest buffer
      entry->queue.pop();
      entry->dropped_count++;
      
      if (m_drop_cb)
        m_drop_cb(id, entry->dropped_count);
    }
    
    entry->queue.push(buffer);
    entry->queue_cv.notify_one();
  }
  
  // Update stats
  m_stats.total_buffers_received++;
  m_stats.total_buffers_distributed += m_outputs.size();
}
```

### 10.3 Memory Pool Considerations

For high-throughput scenarios, consider implementing a memory pool:

```cpp
class buffer_pool
{
public:
  buffer_pool(size_t buffer_size, size_t pool_size);
  
  shared_buffer acquire();
  void release(shared_buffer& buffer);
  
private:
  std::queue<shared_buffer> m_available;
  std::mutex m_mutex;
  size_t m_buffer_size;
};
```

---

## 11. Build System Integration

### 11.1 CMakeLists.txt Structure

```cmake
# Root CMakeLists.txt
project(rist-transcoder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED 
  gstreamer-1.0 
  gstreamer-app-1.0 
  gstreamer-pbutils-1.0
)

# External dependencies
add_subdirectory(external/fltk)
add_subdirectory(external/rist-cpp)
add_subdirectory(external/json)

# Modules
add_subdirectory(source/lib)
add_subdirectory(source/receiver)
add_subdirectory(source/transcoder)
add_subdirectory(source/fanout)
add_subdirectory(source/output)
add_subdirectory(source/config)
add_subdirectory(source/stats)
add_subdirectory(source/ui)

# Main executable
add_executable(rist-transcoder source/main.cpp)
target_link_libraries(rist-transcoder PRIVATE
  library
  receiver
  transcoder
  fanout
  output_handlers
  config
  stats
  ui
)
```

### 11.2 Module CMakeLists.txt Pattern

```cmake
# source/receiver/CMakeLists.txt
add_library(receiver)
target_sources(receiver
  PUBLIC
    FILE_SET CXX_MODULES FILES
    receiver.cppm
)
target_link_libraries(receiver PRIVATE
  library
  ristnet
  ${GSTREAMER_LIBRARIES}
)
```

---

## 12. Testing Strategy

### 12.1 Unit Tests

| Module | Test Coverage |
|--------|--------------|
| `config` | JSON parsing, validation, defaults |
| `fanout` | Buffer distribution, reference counting, drops |
| `shared_buffer` | Lifecycle, cloning, thread safety |

### 12.2 Integration Tests

| Scenario | Description |
|----------|-------------|
| End-to-end | RIST sender → transcoder → RIST receiver verification |
| Multi-output | Single input to 4+ simultaneous outputs |
| Reconnection | Network interruption and recovery |
| Hot-reconfig | Change bitrate/encoder while running |

### 12.3 Performance Tests

| Metric | Target |
|--------|--------|
| Latency | < 100ms end-to-end at 1080p60 |
| CPU Usage | < 50% per 1080p60 stream (hardware encoding) |
| Memory | < 500MB baseline + 100MB per output |
| Throughput | Support 4K60 input with 4 simultaneous outputs |

---

## 13. Future Extensions

### 13.1 Planned Features

1. **Dynamic resolution adaptation** based on network conditions
2. **Multiple input support** (failover between RIST sources)
3. **Recording capability** to local storage
4. **Web-based monitoring UI** (REST API + WebSocket)
5. **Docker containerization** with Kubernetes deployment
6. **GPU memory sharing** for zero-copy between decode/encode

### 13.2 Protocol Extensions

1. **SRT input** support (in addition to output)
2. **RTMP input** for ingesting from streaming platforms
3. **WebRTC output** for browser-based viewers
4. **HLS/DASH output** for adaptive streaming

---

## 14. Appendix: File Reference

| File | Module | Purpose |
|------|--------|---------|
| `source/lib/lib.cppm` | `library` | Core types, enums, structs |
| `source/receiver/receiver.cppm` | `receiver` | RIST input handling |
| `source/transcoder/transcoder.cppm` | `transcoder` | GStreamer transcoding |
| `source/fanout/fanout.cppm` | `fanout` | Buffer distribution |
| `source/output/output_base.cppm` | `output_base` | Abstract output base class |
| `source/output/output_rist.cppm` | `output_rist` | RIST output |
| `source/output/output_srt.cppm` | `output_srt` | SRT output |
| `source/output/output_rtmp.cppm` | `output_rtmp` | RTMP output |
| `source/output/output_sdp.cppm` | `output_sdp` | SDP/RTP output |
| `source/config/config.cppm` | `config` | JSON configuration |
| `source/stats/stats.cppm` | `stats` | Statistics aggregation |
| `source/ui/ui.cppm` | `ui` | FLTK user interface |
| `source/main.cpp` | - | Application entry point |

---

*This architecture document follows the design patterns established in open-broadcast-encoder, ensuring consistency and interoperability between the two systems.*
