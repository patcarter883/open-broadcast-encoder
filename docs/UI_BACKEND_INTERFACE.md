# UI-Backend Interface Architecture Reference

> **Document Purpose**: Reference for implementing communication between the FLTK UI (frontend) and C++ libraries (backend). This document captures the patterns, threading model, and existing abstractions to prevent rediscovery.
> 
> **Last Updated**: 2026-03-27

---

## 1. Architecture Overview

### 1.1 Three-Layer Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           LAYER 1: FRONTEND (UI)                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │   FLTK UI   │  │  Widgets    │  │   Menus     │  │  Log Displays   │ │
│  │  (ui.h/cpp) │  │ (Callbacks) │  │ (user_data) │  │  (Text Buffers) │ │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └────────┬────────┘ │
│         │                │                │                  │          │
│         └────────────────┴────────────────┴──────────────────┘          │
└────────────────────────────────────┼──────────────────────────────────────┘
                                     │
                                     ▼ calls C API
┌─────────────────────────────────────────────────────────────────────────┐
│                      LAYER 2: C API INTERFACE                            │
│                 encoder_wrapper.h / encoder_wrapper.cpp                   │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │   C ABI Types: CInputMode, CCodec, CEncoder, CStreamConfig       │  │
│  │   Callback Types: LogCallback, StatsCallback, NDIDevicesCallback │  │
│  │   Functions: encoder_start(), ndi_refresh_devices(), etc.        │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────┼──────────────────────────────────────┘
                                     │
                                     ▼ uses
┌─────────────────────────────────────────────────────────────────────────┐
│                      LAYER 3: BACKEND (C++ Libraries)                    │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                          Global State                             │  │
│  │  ┌─────────────┐ ┌──────────────┐ ┌──────────────┐ ┌────────────┐ │  │
│  │  │ library app │ │   encode     │ │transport_mgr │ │ ndi_input  │ │  │
│  │  │(config)     │ │(GStreamer)   │ │(RIST/SRT)    │ │(discovery) │ │  │
│  │  └─────────────┘ └──────────────┘ └──────────────┘ └────────────┘ │  │
│  │                                                                   │  │
│  │  ┌─────────────┐ ┌──────────────┐ ┌──────────────┐                │  │
│  │  │  transport  │ │  statistics  │ │ buffer_dist  │                │  │
│  │  │  manager    │ │  aggregator  │ │ ributor     │                │  │
│  │  └─────────────┘ └──────────────┘ └──────────────┘                │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Why Three Layers?

| Layer | Purpose | Language |
|-------|---------|----------|
| Frontend (UI) | User interaction, widget management | C++ (FLTK) |
| C API Interface | Stable ABI for UI-to-backend communication | C (extern "C") |
| Backend | Pipeline management, encoding, transport | C++20 |

The C API layer provides a stable interface that:
- Prevents C++ ABI issues (name mangling, STL containers)
- Allows future UI implementations (web, Qt, etc.)
- Uses plain C types for compatibility

---

## 2. Interface Specifications

### 2.1 C API Header Location

**File**: `source/encoder_wrapper/encoder_wrapper.h`

This is the canonical interface between frontend and backend.

### 2.2 Type Mappings (C++ ↔ C)

| C++ Type (lib.h) | C Type (encoder_wrapper.h) | Notes |
|------------------|---------------------------|-------|
| `input_mode` enum | `CInputMode` enum | ND, RTSP, SRT, FILE |
| `codec` enum | `CCodec` enum | H264, H265, VP9, AV1 |
| `encoder` enum | `CEncoder` enum | NVIDIA, AMD, INTEL, etc. |
| `transport_protocol` | `CTransportProtocol` | RIST, SRT, RTMP |
| `input_config` | `CInputConfig` struct | Fixed-size char arrays |
| `encode_config` | `CEncodeConfig` struct | Plain data |
| `stream_config` | `CStreamConfig` struct | URL buffers sized to 1024 |
| `stream_stats` | `CStreamStats` struct | Aggregate stats |
| `ndi_input` discovery | `CNDIDevice` struct | Device info |

### 2.3 Callback Types

```c
// Logging callback - thread-safe via Fl::lock in UI
typedef void (*LogCallback)(const char* level, const char* message);

// Statistics callback - called periodically from stats thread
typedef void (*StatsCallback)(const CCumulativeStats* stats);

// NDI device discovery callback - called once with device list
typedef void (*NDIDevicesCallback)(const CNDIDevice* devices, int32_t count);
```

---

## 3. Communication Patterns

### 3.1 Frontend-to-Backend Commands (UI → C API)

**Pattern**: Direct C function calls from UI code

```cpp
// In ui.cpp - button click handler calls C API:
void user_interface::start_encoding() {
    CEncodeConfig cfg;
    cfg.codec = C_CODEC_H264;
    cfg.encoder = C_ENCODER_NVIDIA;
    cfg.bitrate_kbps = 5000;
    // ...
    encoder_set_encode_config(&cfg);
    encoder_start(encode_log_cb);
}
```

**Key C API Functions**:
| Function | Purpose | Called From |
|----------|---------|-------------|
| `encoder_wrapper_init()` | Initialize backend | Application startup |
| `encoder_set_input_config()` | Configure input source | Input selection |
| `encoder_set_encode_config()` | Configure encoding | Encoder settings |
| `encoder_add_stream()` | Add output stream | Stream configuration |
| `encoder_start()` | Begin encoding | Start button |
| `encoder_stop()` | Stop encoding | Stop button |
| `encoder_set_bitrate()` | Adjust bitrate | Adaptive bitrate |
| `ndi_refresh_devices()` | Discover NDI sources | Refresh button |

---

### 3.2 Backend-to-Frontend Data (Callbacks)

**Pattern**: Callbacks registered at initialization, called from backend threads

```cpp
// 1. Register callbacks during initialization
encoder_wrapper_init(
    &transport_log_callback,    // LogCallback for transport
    &stats_update_callback      // StatsCallback for stats updates
);

// 2. Backend calls these from worker threads
void transport_log_callback(const char* level, const char* msg) {
    // Thread-safe via Fl::lock()
    ui.transport_log_append(msg);
}

void stats_update_callback(const CCumulativeStats* stats) {
    // Update statistics display
    ui.update_stats_display(stats);
}
```

**Thread Safety Critical**: Backend callbacks are called from worker threads (GStreamer, RIST, NDI discovery). **MUST** use `Fl::lock()`/`Fl::unlock()`/`Fl::awake()` pattern.

---

### 3.3 NDI Device Discovery Flow

```
User clicks "Refresh NDI" in UI
           │
           ▼
┌─────────────────────────────┐
│ ui::refresh_ndi_devices()   │  UI thread
└────────────┬────────────────┘
             │
             ▼ calls C API
┌─────────────────────────────┐
│ ndi_refresh_devices(cb)     │  C API function
│                             │  Synchronous call
└────────────┬────────────────┘
             │
             ▼ uses
┌─────────────────────────────┐
│ ndi_input::               │  Backend C++
│ refresh_devices()         │  GstDeviceMonitor
└────────────┬────────────────┘
             │ returns vector<string>
             ▼
┌─────────────────────────────┐
│ NDIDevicesCallback          │  C API callback
│ (CNDIDevice* devices,       │  with device list
│  int32_t count)             │
└────────────┬────────────────┘
             │ callback
             ▼
┌─────────────────────────────┐
│ Fl::lock()                  │  UI thread (via Fl::awake)
│ update choice_ndi_input     │
│ Fl::unlock()                │
│ Fl::awake()                 │  Critical!
└─────────────────────────────┘
```

---

## 4. Legacy Global State Pattern (for reference)

**Location**: `source/main.cpp` (legacy standalone mode)

The original application uses global instances for cross-component communication:

```cpp
// Global state declarations (main.cpp)
library app;                          // Shared config/state container
std::unique_ptr<transport> transporter;  // Single output (legacy)
transport_manager g_transport_manager;    // Multi-output (current)
statistics_aggregator g_stats_aggregator;
user_interface ui;
encode* ptr_encoder;                  // Raw pointer for bitrate callbacks
```

**Why globals**: FLTK callbacks require static/global context, and RIST callbacks need access to encoder for bitrate adjustment.

---

### 4.1 UI-to-Backend Command Pattern (Legacy)

**Location**: `source/ui/ui.h:50-62`

UI callbacks are registered via function pointers in `init_ui_callbacks()`:

```cpp
void user_interface::init_ui_callbacks(
    input_config* input_c,           // Config struct for data binding
    encode_config* encode_c,
    output_config* output_c,
    std::function<void()> run_func,           // Start streaming
    std::function<void()> stop_func,          // Stop streaming
    FuncPtr ndi_refresh_funcptr,     // Refresh NDI sources
    std::function<void()> run_transport_func, // Initialize transport
    std::function<void()> preview_func       // Preview input
);
```

---

### 2.3 Backend-to-UI Data Pattern

**Thread-safe UI updates** (CRITICAL - causes hangs if done wrong):

```cpp
// CORRECT PATTERN:
Fl::lock();        // NOT ui.lock()
// ... update FLTK widgets ...
Fl::unlock();
Fl::awake();       // REQUIRED to wake UI thread - NEVER FORGET
```

**Example**: Populating NDI device list (`source/ui/ui.cpp`)
```cpp
void user_interface::add_ndi_choices(const std::vector<std::string>& choice_names)
{
  Fl::lock();
  for (const auto& choice_name : choice_names) {
    ndi_device_names.push_back(choice_name);
    choice_ndi_input->add(choice_name.c_str());
  }
  Fl::unlock();
  Fl::awake();  // Critical: wakes FLTK main thread
}
```

---

### 2.4 FLTK Callback Macro Pattern

**Header**: `FL/fl_callback_macros.H`

For UI callbacks that need to call methods on the UI class:

```cpp
// Pattern: FL_METHOD_CALLBACK_N(widget, class, instance, method, arg1_type, arg1, arg2_type, arg2, ...)

FL_METHOD_CALLBACK_2(choice_input_protocol,     // Widget
                     user_interface, this,      // Class, instance
                     choose_input_protocol,       // Method
                     input_config*, input_c,     // Args with types
                     FuncPtr, ndi_refresh_funcptr);
```

---

### 2.5 Menu Item User Data Pattern

**Location**: `source/ui/ui.h:106-115` (for input protocols), `ui.h:189-197` (for encoders)

Enum values are stored as `user_data()` for type-safe selection:

```cpp
// In ui.h - store enum as void*
static Fl_Menu_Item menu_choice_input_protocol[] = {
  {"NDI", 0, 0, (void*)(static_cast<long>(input_mode::ndi)), ...},
  {"MPEGTS", 0, 0, (void*)(static_cast<long>(input_mode::mpegts)), ...},
  ...
};

// Retrieval - cast back to enum
void user_interface::choose_input_protocol(...) {
  auto mode = static_cast<input_mode>(
      reinterpret_cast<uintptr_t>(choice->mvalue()->user_data()));
  input_c->mode = mode;
}
```

---

## 3. Configuration Data Structures

**Location**: `source/lib/lib.h`

All shared state is in config structs within the `library` class:

| Struct | Purpose | Key Fields |
|--------|---------|------------|
| `input_config` | Input source selection | `input_mode mode`, `std::string ndi_source_name` |
| `encode_config` | Encoding parameters | `encoder encoder_type`, `codec codec_type`, `int bitrate`, `int width/height/fps` |
| `output_config` | Legacy output settings | Protocol, address, buffer settings |
| `stream_config` | Multi-output per-stream | `protocol`, `address`, `buffer_*`, `timing_mode` |
| `stream_stats` | Per-stream statistics | `quality`, `bandwidth`, `rtt`, `quality_band` |
| `cumulative_stats` | Aggregated statistics | `current_bitrate`, `recommended_bitrate`, `avg_quality` |
| `library` | Container for all state | Holds all config structs and stats |

---

## 4. NDI Input Library Interface

**Location**: `source/ndi_input/ndi_input.h`

### 4.1 Current Discovery Pattern

```cpp
class ndi_input {
public:
  // Constructor takes log callback for decoupled logging
  explicit ndi_input(log_func_ptr log_cb = nullptr);
  
  // Discovery - returns vector of display names
  auto refresh_devices() const -> std::vector<std::string>;
  
  // Preview specific source
  auto preview(const std::string& source_name) -> void;
  
private:
  // Uses GstDeviceMonitor in background thread
  mutable std::thread device_monitor_thread;
  void device_monitor_loop();
};
```

### 4.2 Threading Model

- `device_monitor_thread`: Runs `GstDeviceMonitor` with 1-second sleep loop
- Device list retrieved via `gst_device_monitor_get_devices()`
- Returns `std::vector<std::string>` of display names

---

## 5. Communication Flow Examples

### 5.1 NDI Source Discovery Flow

```
User clicks "Refresh NDI" button
           │
           ▼
┌──────────────────────┐
│  refresh_ndi_devices │  (main.cpp callback)
│     function ptr     │  passed to ui.init_ui_callbacks()
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│    ndi_input::      │
│  refresh_devices()   │  Returns vector<string> of source names
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│ add_ndi_choices()    │  (ui.cpp)
│   Fl::lock()         │  Thread-safe UI update
│   update choice_ndi_input
│   Fl::unlock()       │
│   Fl::awake()        │
└──────────────────────┘
```

### 5.2 Adaptive Bitrate Flow

```
RIST Statistics Callback
           │
           ▼
┌──────────────────────┐
│   rist_stats_cb()    │  (main.cpp - static callback)
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│got_rist_statistics() │  (stats.h - quality calculation)
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│ ptr_encoder->        │  Via global encoder pointer
│ set_encode_bitrate() │
└──────────────────────┘
```

---

## 6. Key Implementation Rules

### 6.1 Thread Safety (CRITICAL)

| Rule | Code | Consequence if Wrong |
|------|------|---------------------|
| Use `Fl::lock()` NOT `ui.lock()` | `Fl::lock();` | UI updates fail |
| Always call `Fl::awake()` after unlock | `Fl::unlock(); Fl::awake();` | **UI freezes** |
| Background threads only touch UI via this pattern | See examples above | Crashes/hangs |

### 6.2 Include Order

```cpp
// 1. Standard library headers
#include <vector>
#include <string>

// 2. Third-party headers (GStreamer, FLTK)
#include <gst/gst.h>
#include <FL/Fl.H>

// 3. Project headers (lib.h FIRST, then others)
#include "lib/lib.h"
#include "ui/ui.h"
```

### 6.3 Naming Conventions

- `lower_case` for classes, functions, enums, variables
- `left_pointer_alignment`: `type* ptr` (NOT `type *ptr`)
- 80 column limit, 2-space indent

---

## 7. Extending the Interface

### 7.1 Adding New UI-to-Backend Commands

1. Define callback function in `main.cpp`
2. Add parameter to `init_ui_callbacks()` in `ui.h`
3. Wire up via `FL_METHOD_CALLBACK_N` macro in `ui.cpp`
4. Implement handler in relevant component

### 7.2 Adding New Backend-to-UI Data Flows

1. Component calls thread-safe UI update method
2. UI method uses `Fl::lock()` / `Fl::unlock()` / `Fl::awake()`
3. Update FLTK widget state

### 7.3 Adding New Menu Selections

1. Add enum value to appropriate enum in `lib.h`
2. Add menu item with cast to `void*` in `ui.h`
3. Implement retrieval via `reinterpret_cast<uintptr_t>()` in handler

---

## 8. Reference Files

| File | Responsibility | Key Sections |
|------|----------------|--------------|
| `source/main.cpp` | Global state, callback wiring, main loop | Lines 1-100 (globals), 200-300 (callbacks) |
| `source/ui/ui.h` | UI class definition, menu items | Lines 50-70 (callbacks), 100-200 (menus) |
| `source/ui/ui.cpp` | UI implementation, thread-safe updates | Lines 100-150 (NDI choices), 200+ (callbacks) |
| `source/ndi_input/ndi_input.h` | NDI discovery interface | Lines 1-50 (class definition) |
| `source/ndi_input/ndi_input.cpp` | NDI implementation | Lines 50+ (discovery thread) |
| `source/lib/lib.h` | Shared types and config structs | Lines 1-100 (enums), 100-200 (structs) |

---

## 9. Implementation Plan: NDI Source List Population

### Phase 1: Current State Analysis

**Existing Implementation**:
- `ndi_input::refresh_devices()` already exists in `ndi_input.cpp`
- Returns `std::vector<std::string>` of device display names
- `encoder_wrapper.h` has `ndi_refresh_devices(NDIDevicesCallback cb)` function

**UI Side**:
- `user_interface::add_ndi_choices()` exists in `ui.cpp`
- Uses `Fl::lock()`/`Fl::unlock()`/`Fl::awake()` pattern
- Currently called from `refresh_ndi_devices()` in `main.cpp`

### Phase 2: Implementation Steps

#### Step 1: Complete C API Implementation

**File**: `source/encoder_wrapper/encoder_wrapper.cpp`

Ensure `ndi_refresh_devices()` is fully implemented:

```cpp
int32_t ndi_refresh_devices(NDIDevicesCallback cb) {
    if (g_state == nullptr || cb == nullptr) {
        return -1;
    }
    
    // Get devices from ndi_input
    auto devices = g_state->p_ndi->refresh_devices();
    
    // Convert to C types
    std::vector<CNDIDevice> c_devices;
    c_devices.reserve(devices.size());
    
    for (const auto& name : devices) {
        CNDIDevice dev;
        strncpy(dev.name, name.c_str(), sizeof(dev.name) - 1);
        dev.name[sizeof(dev.name) - 1] = '\0';
        // ... fill other fields
        c_devices.push_back(dev);
    }
    
    // Call callback with results
    if (!c_devices.empty()) {
        cb(c_devices.data(), static_cast<int32_t>(c_devices.size()));
    }
    
    return 0;
}
```

#### Step 2: UI Integration

**File**: `source/ui/ui.cpp`

Add method to handle NDI device callback:

```cpp
void user_interface::handle_ndi_devices(const CNDIDevice* devices, int32_t count) {
    Fl::lock();
    clear_ndi_choices();
    for (int32_t i = 0; i < count; ++i) {
        ndi_device_names.push_back(devices[i].name);
        choice_ndi_input->add(devices[i].name);
    }
    Fl::unlock();
    Fl::awake();
}
```

#### Step 3: Button Handler

**File**: `source/ui/ui.cpp`

Update refresh button handler:

```cpp
void user_interface::refresh_ndi_devices() {
    // Call C API
    ndi_refresh_devices([](const CNDIDevice* devices, int32_t count) {
        ui.handle_ndi_devices(devices, count);
    });
}
```

### Phase 3: Thread Safety Verification

**Critical Checklist**:
- [ ] `ndi_input::refresh_devices()` runs in UI thread (synchronous)
- [ ] `NDIDevicesCallback` is called before function returns
- [ ] UI callback uses `Fl::lock()` before accessing FLTK widgets
- [ ] UI callback uses `Fl::unlock()` and `Fl::awake()` after updates
- [ ] No background thread directly accesses FLTK

### Phase 4: Testing

1. **Unit Test**: Call `ndi_refresh_devices()` and verify callback receives correct data
2. **UI Test**: Click "Refresh" button and verify dropdown populates
3. **Thread Safety Test**: Rapid refresh clicks, verify no crashes
4. **Edge Cases**: No devices, single device, many devices

---

## 10. Frontend Library Interface Specification

### 10.1 Header Location

**File**: `source/encoder_wrapper/encoder_wrapper.h`

This is the stable C ABI interface for all frontend communication.

### 10.2 Command Functions (Frontend → Backend)

| Function Signature | Description |
|------------------|-------------|
| `int32_t encoder_wrapper_init(LogCallback log_cb, StatsCallback stats_cb)` | Initialize backend with callbacks |
| `int32_t encoder_wrapper_destroy(void)` | Cleanup and shutdown |
| `int32_t encoder_set_input_config(const CInputConfig* cfg)` | Configure input source |
| `int32_t encoder_set_encode_config(const CEncodeConfig* cfg)` | Configure encoder |
| `int32_t encoder_add_stream(const CStreamConfig* cfg)` | Add output stream |
| `int32_t encoder_remove_stream(const char* stream_id)` | Remove output stream |
| `int32_t encoder_start(LogCallback on_encode_log)` | Begin encoding |
| `int32_t encoder_stop(void)` | Stop encoding |
| `int32_t encoder_set_bitrate(int32_t bitrate_kbps)` | Adjust bitrate |
| `int32_t ndi_refresh_devices(NDIDevicesCallback cb)` | Discover NDI sources |
| `int32_t encoder_open_sdp_file(const char* path)` | Load SDP file |

### 10.3 Callback Types (Backend → Frontend)

```c
// Logging - thread-safe via Fl::lock in implementation
typedef void (*LogCallback)(const char* level, const char* message);

// Statistics - called periodically from stats thread
typedef void (*StatsCallback)(const CCumulativeStats* stats);

// NDI devices - called once per refresh_devices call
typedef void (*NDIDevicesCallback)(const CNDIDevice* devices, int32_t count);
```

### 10.4 Data Types

All types use fixed-size buffers for ABI stability:
- `char url[1024]` - URLs and paths
- `char name[256]` - Device/source names
- `char stream_id[256]` - Stream identifiers
- `int32_t` for all integers (fixed width)

---

## 11. Backend Library Architecture

### 11.1 Component Hierarchy

```
encoder_wrapper.cpp (C API layer)
├── library (config container)
│   ├── input_config
│   ├── encode_config
│   └── output_config
├── ndi_input (GStreamer device monitor)
│   └── GstDeviceMonitor
├── encode (GStreamer pipeline)
│   ├── Pipeline: source → encoder → mux → appsink
│   └── Dynamic bitrate control via set_encode_bitrate()
├── transport_manager (multi-output)
│   ├── buffer_distributor (shared_ptr zero-copy)
│   └── transport (RIST/SRT/RTMP)
└── statistics_aggregator
    └── Per-stream and cumulative stats
```

### 11.2 Threading Model

| Component | Thread | Purpose |
|-----------|--------|---------|
| FLTK | Main/UI | UI updates, user input |
| `encode` | encode_thread | GStreamer pipeline loop |
| `ndi_input` | device_monitor_thread | Device discovery |
| `transport` | RIST internal | Network I/O, stats callbacks |
| Stats | stats_thread | Periodic stats aggregation |

### 11.3 Callback Threading Rules

| Callback | Called From | Thread Safety |
|----------|-------------|---------------|
| `LogCallback` | encode_thread, transport | Must use `Fl::lock/unlock/awake` |
| `StatsCallback` | stats_thread | Must use `Fl::lock/unlock/awake` |
| `NDIDevicesCallback` | UI thread (synchronous) | Uses `Fl::lock/unlock/awake` |

---

## 12. Build Commands

```bash
# Use cmake-skill (never direct cmake)
cmake --build build -t format-fix    # Before commit
cmake --build build -t run-exe       # Run executable

# Developer mode
cmake --preset=dev
cmake --build --preset=dev
```

---

*Document created: 2026-03-27*
*Project: Open Broadcast Encoder*
*Architecture: C++20 video streaming encoder with FLTK GUI, GStreamer pipelines, and RIST transport*
