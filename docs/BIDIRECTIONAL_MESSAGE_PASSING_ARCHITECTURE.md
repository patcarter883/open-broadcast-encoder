# Bidirectional Message Passing Architecture

## Executive Summary

This document defines the architecture for bidirectional message passing between the main thread and worker threads in the Open Broadcast Encoder. The system enables:

- **Command dispatch** from UI to encoder with acknowledgment
- **Event streaming** from encoder to UI for real-time updates
- **Query/Response** patterns for state inspection
- **Thread-safe communication** without blocking the UI

The architecture is designed for a media encoding pipeline where:
- The **main thread** hosts the FLTK UI and user interaction
- **Worker threads** handle GStreamer pipeline execution, NDI monitoring, and RIST transport
- **Bidirectional channels** enable coordinated control and monitoring

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              MAIN THREAD                                     │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐  │
│  │   FLTK UI   │    │  UI State   │    │   Command   │    │   Event     │  │
│  │  (ui.h)     │◀──▶│   Manager   │◀──▶│   Queue     │◀───│   Handler   │  │
│  └─────────────┘    └─────────────┘    └──────┬──────┘    └─────────────┘  │
│         ▲                                       │                            │
│         │                                       │                            │
│         │ User Actions                          │ Commands                   │
│         │                                       ▼                            │
│  ┌─────────────┐                         ┌─────────────┐                     │
│  │  Callbacks  │                         │   Sender    │                     │
│  │  (FLTK)     │                         │  Channel    │                     │
│  └─────────────┘                         └──────┬──────┘                     │
│                                                 │                            │
└─────────────────────────────────────────────────┼────────────────────────────┘
                                                  │
                       ═══════════════════════════╪════════════════════════════
                                                  │
                                                  ▼
┌─────────────────────────────────────────────────┼────────────────────────────┐
│                             WORKER THREADS       │                            │
│                                                  │                            │
│  ┌─────────────────────────────────────────┐    │                            │
│  │           ENCODE THREAD                 │    │                            │
│  │  ┌─────────────┐    ┌─────────────┐     │    │                            │
│  │  │   Command   │    │   GStreamer │     │    │                            │
│  │  │   Queue     │────▶│   Pipeline  │     │    │                            │
│  │  │  (Consumer) │    │  (encode.h) │     │    │                            │
│  │  └─────────────┘    └──────┬──────┘     │    │                            │
│  │                            │            │    │                            │
│  │                            ▼            │    │                            │
│  │  ┌─────────────┐    ┌─────────────┐     │    │                            │
│  │  │   Event     │◀───│   Encoder   │     │    │                            │
│  │  │   Queue     │    │   State     │     │    │                            │
│  │  │  (Producer) │    │             │     │    │                            │
│  │  └──────┬──────┘    └─────────────┘     │    │                            │
│  │         │                               │    │                            │
│  │         │ Events                         │    │                            │
│  │         │                               │    │                            │
│  │  ┌──────┴──────┐                       │    │                            │
│  │  │   Sender    │───────────────────────┘    │                            │
│  │  │  Channel    │                            │                            │
│  │  └─────────────┘                            │                            │
│  └─────────────────────────────────────────┘    │                            │
│                                                 │                            │
│  ┌─────────────────────────────────────────┐    │                            │
│  │         TRANSPORT THREAD                │    │                            │
│  │  ┌─────────────┐    ┌─────────────┐     │    │                            │
│  │  │   Command   │    │    RIST     │     │    │                            │
│  │  │   Queue     │────▶│  Transport  │     │    │                            │
│  │  │  (Consumer) │    │(transport.h)│     │    │                            │
│  │  └─────────────┘    └──────┬──────┘     │    │                            │
│  │                            │            │    │                            │
│  │                            ▼            │    │                            │
│  │  ┌─────────────┐    ┌─────────────┐     │    │                            │
│  │  │   Event     │◀───│   RIST      │     │    │                            │
│  │  │   Queue     │    │ Statistics  │     │    │                            │
│  │  │  (Producer) │    │             │     │    │                            │
│  │  └──────┬──────┘    └─────────────┘     │    │                            │
│  │         │                               │    │                            │
│  │         │ Stats Events                   │    │                            │
│  │         │                               │    │                            │
│  │  ┌──────┴──────┐                       │    │                            │
│  │  │   Sender    │───────────────────────┘    │                            │
│  │  │  Channel    │                            │                            │
│  │  └─────────────┘                            │                            │
│  └─────────────────────────────────────────┘    │                            │
│                                                 │                            │
│  ┌─────────────────────────────────────────┐    │                            │
│  │           NDI THREAD                    │    │                            │
│  │  ┌─────────────┐    ┌─────────────┐     │    │                            │
│  │  │   Command   │    │    NDI      │     │    │                            │
│  │  │   Queue     │────▶│   Monitor   │     │    │                            │
│  │  │  (Consumer) │    │(ndi_input.h)│     │    │                            │
│  │  └─────────────┘    └──────┬──────┘     │    │                            │
│  │                            │            │    │                            │
│  │                            ▼            │    │                            │
│  │  ┌─────────────┐    ┌─────────────┐     │    │                            │
│  │  │   Event     │◀───│   Source    │     │    │                            │
│  │  │   Queue     │    │  Discovery  │     │    │                            │
│  │  │  (Producer) │    │             │     │    │                            │
│  │  └──────┬──────┘    └─────────────┘     │    │                            │
│  │         │                               │    │                            │
│  │         │ Source Events                  │    │                            │
│  │         │                               │    │                            │
│  │  ┌──────┴──────┐                       │    │                            │
│  │  │   Sender    │───────────────────────┘    │                            │
│  │  │  Channel    │                            │                            │
│  │  └─────────────┘                            │                            │
│  └─────────────────────────────────────────┘    │                            │
│                                                 │                            │
└─────────────────────────────────────────────────┼────────────────────────────┘
                                                  │
                       ═══════════════════════════╧════════════════════════════
                                                  │
                                                  ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         SYNCHRONIZATION PRIMITIVES                           │
│                                                                              │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐              │
│  │   Mutex Lock    │  │ Condition Var   │  │   Atomic Flag   │              │
│  │  (std::mutex)   │  │(std::condition) │  │(std::atomic_bool)│              │
│  │                 │  │                 │  │                 │              │
│  │  Lock-free for  │  │  Wake on new    │  │  Shutdown       │              │
│  │  event queue    │  │  command/event  │  │  signaling      │              │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘              │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Message Types

### Commands

Commands are sent from the main thread to worker threads. They represent actions to be performed.

```cpp
enum class command_type {
  // Encode commands
  encode_start,
  encode_stop,
  encode_pause,
  encode_set_bitrate,
  encode_set_resolution,
  encode_set_framerate,
  encode_apply_preset,
  
  // Transport commands
  transport_connect,
  transport_disconnect,
  transport_set_destination,
  
  // NDI commands
  ndi_refresh_sources,
  ndi_select_source,
  
  // System commands
  shutdown
};

struct command {
  command_type type;
  uint64_t command_id;           // Unique ID for correlation
  std::chrono::steady_clock::time_point timestamp;
  std::any payload;              // Type-safe payload container
};
```

### Events

Events are sent from worker threads to the main thread. They represent state changes or notifications.

```cpp
enum class event_type {
  // Encode events
  encode_started,
  encode_stopped,
  encode_error,
  encode_bitrate_changed,
  encode_stats_update,
  
  // Transport events
  transport_connected,
  transport_disconnected,
  transport_error,
  transport_stats_update,
  
  // NDI events
  ndi_sources_updated,
  ndi_source_selected,
  
  // Pipeline events
  pipeline_state_changed,
  pipeline_element_error,
  
  // System events
  worker_thread_error
};

struct event {
  event_type type;
  uint64_t correlation_id;       // Links to originating command (if any)
  std::chrono::steady_clock::time_point timestamp;
  std::any payload;
};
```

### Queries

Queries are special commands that expect a response. Used for state inspection.

```cpp
enum class query_type {
  query_encode_status,
  query_transport_status,
  query_pipeline_state,
  query_statistics,
  query_available_encoders,
  query_available_sources
};

struct query {
  query_type type;
  uint64_t query_id;
  std::chrono::steady_clock::time_point timestamp;
  std::any parameters;
};

struct query_response {
  uint64_t query_id;
  bool success;
  std::any result;
  std::string error_message;
};
```

## Command Pattern

### Command Queue

Thread-safe queue for command dispatch:

```cpp
class command_queue {
public:
  void push(command&& cmd);
  std::optional<command> try_pop();
  std::optional<command> pop_with_timeout(std::chrono::milliseconds timeout);
  void shutdown();
  
private:
  std::queue<command> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic_bool shutdown_{false};
};
```

### Command Handler

Interface for command processing:

```cpp
class command_handler {
public:
  virtual ~command_handler() = default;
  virtual void handle_command(const command& cmd) = 0;
  virtual bool can_handle(command_type type) const = 0;
};

class encode_command_handler : public command_handler {
public:
  void handle_command(const command& cmd) override {
    switch (cmd.type) {
      case command_type::encode_start:
        handle_start(cmd);
        break;
      case command_type::encode_set_bitrate:
        handle_set_bitrate(cmd);
        break;
      // ...
    }
  }
  
private:
  void handle_start(const command& cmd);
  void handle_set_bitrate(const command& cmd);
  void send_acknowledgment(const command& cmd, bool success, 
                          const std::string& message = {});
};
```

### Command Flow

```
Main Thread                    Worker Thread
───────────                    ─────────────
     │                               │
     │  1. Create command            │
     │  2. Assign command_id         │
     │  3. Push to queue             │
     │──────────────────────────────▶│
     │                               │
     │                               │  4. Wait on condition var
     │                               │  5. Pop command
     │                               │  6. Process command
     │                               │  7. Send acknowledgment event
     │◀──────────────────────────────│
     │                               │
     │  8. Receive acknowledgment    │
     │  9. Update UI state           │
     │                               │
```

## Event Pattern

### Event Queue

Lock-free SPSC (Single Producer Single Consumer) queue for events:

```cpp
template<typename T, size_t Capacity>
class lockfree_spsc_queue {
public:
  bool try_push(const T& item);
  bool try_push(T&& item);
  bool try_pop(T& item);
  bool empty() const;
  
private:
  std::array<T, Capacity> buffer_;
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
};

using event_queue = lockfree_spsc_queue<event, 1024>;
```

### Event Dispatcher

Routes events to appropriate handlers:

```cpp
class event_dispatcher {
public:
  using handler = std::function<void(const event&)>;
  
  void register_handler(event_type type, handler h);
  void dispatch(const event& evt);
  void process_pending();
  
private:
  std::unordered_map<event_type, std::vector<handler>> handlers_;
  event_queue queue_;
};
```

### Event Flow

```
Worker Thread                  Main Thread
─────────────                  ───────────
     │                               │
     │  1. Detect state change       │
     │  2. Create event              │
     │  3. Try push to queue         │
     │──────────────────────────────▶│ (lock-free)
     │                               │
     │                               │  4. Fl::awake() called
     │                               │  5. Process event queue
     │                               │  6. Dispatch to handlers
     │                               │  7. Update UI (with Fl::lock)
     │                               │
```

## State Synchronization

### Shared State

Atomic state variables accessible from both threads:

```cpp
struct shared_encode_state {
  std::atomic<gst_state> pipeline_state{gst_state::null};
  std::atomic<uint32_t> current_bitrate_kbps{0};
  std::atomic<uint32_t> target_bitrate_kbps{0};
  std::atomic<bool> encoding_active{false};
  std::atomic<uint64_t> frames_encoded{0};
  std::atomic<uint64_t> bytes_encoded{0};
};

struct shared_transport_state {
  std::atomic<transport_status> status{transport_status::disconnected};
  std::atomic<uint32_t> bandwidth_kbps{0};
  std::atomic<uint32_t> quality{0};
  std::atomic<uint64_t> packets_sent{0};
  std::atomic<uint64_t> packets_retransmitted{0};
};
```

### State Update Protocol

1. **Worker thread** updates atomic state immediately
2. **Worker thread** queues event for UI notification
3. **Main thread** reads atomic state for UI display
4. **Main thread** processes event for side effects (logging, etc.)

### State Consistency

For non-atomic compound state, use sequence numbers:

```cpp
struct compound_state {
  std::atomic<uint64_t> sequence_number{0};
  std::mutex data_mutex;
  state_data data;
  
  state_snapshot get_snapshot() {
    std::lock_guard<std::mutex> lock(data_mutex);
    return {sequence_number.load(), data};
  }
  
  void update(const state_data& new_data) {
    std::lock_guard<std::mutex> lock(data_mutex);
    data = new_data;
    sequence_number.fetch_add(1);
  }
};
```

## Threading Model

### Thread Hierarchy

```
main_thread (priority: normal)
├── fltk_event_loop
├── command_dispatcher
└── event_processor

encode_thread (priority: high)
├── command_consumer
├── gstreamer_pipeline
└── event_producer

transport_thread (priority: high)
├── command_consumer
├── rist_sender
└── event_producer (stats)

ndi_thread (priority: normal)
├── command_consumer
├── source_monitor
└── event_producer
```

### Synchronization Rules

1. **Main Thread → Worker**: Command queue with condition variable
2. **Worker → Main Thread**: Lock-free event queue + Fl::awake()
3. **Shared State**: Atomic variables for simple types, mutex for complex
4. **Shutdown**: Atomic flag + condition variable broadcast

### Thread-Safe UI Updates

```cpp
// From worker thread
void update_ui_from_worker(const event& evt) {
  // Lock-free push to event queue
  if (event_queue_.try_push(evt)) {
    // Wake main thread (thread-safe)
    Fl::awake();
  } else {
    // Queue full - log and continue
    log_dropped_event(evt);
  }
}

// In main thread (FLTK idle callback)
void process_events() {
  Fl::lock();
  
  event evt;
  while (event_queue_.try_pop(evt)) {
    dispatch_event(evt);
  }
  
  Fl::unlock();
}
```

## Error Handling

### Error Classification

| Category | Source | Handling |
|----------|--------|----------|
| Command Error | Invalid parameters | Send failure acknowledgment |
| Execution Error | Pipeline failure | Send error event + attempt recovery |
| System Error | Resource exhaustion | Send error event + graceful degrade |
| Timeout Error | Unresponsive worker | Send alert + offer retry/abort |

### Error Event Structure

```cpp
struct error_info {
  error_category category;
  error_severity severity;
  std::string message;
  std::string component;
  std::chrono::steady_clock::time_point timestamp;
  std::optional<uint64_t> correlated_command_id;
  std::optional<int> retry_count;
};

enum class error_severity {
  warning,    // Log and continue
  error,      // Notify user, may recover
  critical    // Stop operation, require intervention
};
```

### Recovery Strategies

```cpp
class error_recovery_manager {
public:
  void handle_error(const error_info& error);
  
private:
  void attempt_recovery(const error_info& error);
  void escalate_error(const error_info& error);
  void notify_user(const error_info& error);
  
  std::unordered_map<error_category, recovery_strategy> strategies_;
  std::atomic<int> consecutive_errors_{0};
};
```

## Performance Considerations

### Lock-Free Queue Implementation

```cpp
template<typename T, size_t Capacity>
class alignas(64) lockfree_spsc_queue {
  // Cache-line aligned to prevent false sharing
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
  
public:
  bool try_push(const T& item) {
    const size_t current_tail = tail_.load(std::memory_order_relaxed);
    const size_t next_tail = (current_tail + 1) & (Capacity - 1);
    
    if (next_tail == head_.load(std::memory_order_acquire)) {
      return false; // Queue full
    }
    
    buffer_[current_tail] = item;
    tail_.store(next_tail, std::memory_order_release);
    return true;
  }
  
  bool try_pop(T& item) {
    const size_t current_head = head_.load(std::memory_order_relaxed);
    
    if (current_head == tail_.load(std::memory_order_acquire)) {
      return false; // Queue empty
    }
    
    item = buffer_[current_head];
    head_.store((current_head + 1) & (Capacity - 1), std::memory_order_release);
    return true;
  }
  
private:
  std::array<T, Capacity> buffer_;
  alignas(64) std::atomic<size_t> head_{0};
  alignas(64) std::atomic<size_t> tail_{0};
};
```

### Memory Allocation Strategy

- Pre-allocate event/command objects in pools
- Use `std::variant` instead of `std::any` where possible
- Avoid allocations in hot paths

### Batch Processing

```cpp
void process_event_batch(size_t max_events = 100) {
  size_t processed = 0;
  event evt;
  
  while (processed < max_events && event_queue_.try_pop(evt)) {
    dispatch_event(evt);
    ++processed;
  }
  
  // Update UI once per batch
  if (processed > 0) {
    ui_redraw();
  }
}
```

## Security Considerations

### Message Validation

```cpp
class message_validator {
public:
  bool validate_command(const command& cmd) {
    // Check command type bounds
    if (static_cast<int>(cmd.type) < 0 || 
        static_cast<int>(cmd.type) > static_cast<int>(command_type::max_value)) {
      return false;
    }
    
    // Validate payload type matches command type
    if (!validate_payload_type(cmd.type, cmd.payload)) {
      return false;
    }
    
    // Check timestamp (reject stale commands)
    auto age = std::chrono::steady_clock::now() - cmd.timestamp;
    if (age > std::chrono::seconds(30)) {
      return false;
    }
    
    return true;
  }
};
```

### Resource Limits

```cpp
struct queue_limits {
  static constexpr size_t max_commands = 1000;
  static constexpr size_t max_events = 10000;
  static constexpr size_t max_payload_size = 64 * 1024; // 64KB
};
```

## Example Flows

### Flow 1: Start Encoding

```
┌──────┐          ┌──────────────┐          ┌──────────────┐
│  UI  │          │ CommandQueue │          │ EncodeThread │
└──┬───┘          └──────┬───────┘          └──────┬───────┘
   │                     │                         │
   │  1. User clicks     │                         │
   │     "Start"         │                         │
   │                     │                         │
   │  2. Create command  │                         │
   │     {encode_start,  │                         │
   │      id: 1234}      │                         │
   │                     │                         │
   │  3. push(command)   │                         │
   │────────────────────▶│                         │
   │                     │                         │
   │  4. Return success  │                         │
   │◀────────────────────│                         │
   │  (async, non-block) │                         │
   │                     │                         │
   │                     │  5. notify_one()        │
   │                     │────────────────────────▶│
   │                     │                         │
   │                     │                         │  6. pop()
   │                     │◀────────────────────────│
   │                     │                         │
   │                     │                         │  7. Validate
   │                     │                         │     command
   │                     │                         │
   │                     │                         │  8. Start pipeline
   │                     │                         │     gst_element_set_state()
   │                     │                         │
   │                     │                         │  9. Create event
   │                     │                         │     {encode_started,
   │                     │                         │      correlation: 1234}
   │                     │                         │
   │                     │                         │  10. push_event()
   │  11. Fl::awake()    │◀────────────────────────│
   │◀────────────────────│                         │
   │                     │                         │
   │  12. Process event  │                         │
   │      queue          │                         │
   │                     │                         │
   │  13. Update UI      │                         │
   │      (show playing) │                         │
   │                     │                         │
   ▼                     ▼                         ▼
```

### Flow 2: Adaptive Bitrate Adjustment

```
┌──────────────┐          ┌──────────────┐          ┌──────────────┐
│ TransportThread          │ EventQueue   │          │     UI       │
└──────┬───────┘          └──────┬───────┘          └──────┬───────┘
       │                         │                         │
       │  1. RIST callback       │                         │
       │     quality dropped     │                         │
       │     to 85%              │                         │
       │                         │                         │
       │  2. Calculate new       │                         │
       │     bitrate             │                         │
       │                         │                         │
       │  3. Create event        │                         │
       │     {transport_stats,   │                         │
       │      quality: 85,       │                         │
       │      suggested_bitrate  │                         │
       │      4500}              │                         │
       │                         │                         │
       │  4. push_event()        │                         │
       │────────────────────────▶│                         │
       │                         │                         │
       │  5. Fl::awake()         │────────────────────────▶│
       │                         │                         │
       │                         │                         │  6. Process stats
       │                         │                         │
       │                         │                         │  7. Decision:
       │                         │                         │     reduce bitrate
       │                         │                         │
       │                         │                         │  8. Create command
       │                         │                         │     {encode_set_bitrate,
       │                         │                         │      value: 4500}
       │                         │                         │
       │                         │                         │
       │  9. push_command()      │                         │
       │◀────────────────────────│                         │
       │                         │                         │
       │  10. Apply bitrate      │                         │
       │      g_object_set()     │                         │
       │      on encoder         │                         │
       │                         │                         │
       ▼                         ▼                         ▼
```

### Flow 3: Error Recovery

```
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ EncodeThread         │ EventQueue│    │    UI    │    │RecoveryManager
└────┬─────┘    └────┬─────┘    └────┬─────┘    └────┬─────┘
     │               │               │               │
     │  1. GST_ERROR │               │               │
     │     from      │               │               │
     │     pipeline  │               │               │
     │               │               │               │
     │  2. Create    │               │               │
     │     error     │               │               │
     │     event     │               │               │
     │               │               │               │
     │  3. push      │               │               │
     │──────────────▶│               │               │
     │               │               │               │
     │               │  4. notify    │               │
     │               │──────────────▶│               │
     │               │               │               │
     │               │               │  5. Display   │
     │               │               │     error     │
     │               │               │     dialog    │
     │               │               │               │
     │               │               │  6. Call      │
     │               │               │     recovery  │
     │               │               │     manager   │
     │               │               │──────────────▶│
     │               │               │               │
     │               │               │               │  7. Check
     │               │               │               │     retry count
     │               │               │               │
     │               │               │               │  8. Decision:
     │               │               │               │     retry allowed
     │               │               │               │
     │  9. Create    │               │               │
     │     restart   │               │               │
     │     command   │               │               │
     │◀──────────────│               │               │
     │               │               │               │
     │  10. Restart  │               │               │
     │      pipeline │               │               │
     │               │               │               │
     ▼               ▼               ▼               ▼
```

## Implementation Checklist

### Core Infrastructure

- [ ] `message.h` - Base message types
- [ ] `command_queue.h/.cpp` - Thread-safe command queue
- [ ] `event_queue.h` - Lock-free event queue
- [ ] `message_dispatcher.h/.cpp` - Routing logic

### Thread Communication

- [ ] `command_sender.h` - Main thread command API
- [ ] `event_receiver.h` - Main thread event API
- [ ] `command_processor.h` - Worker thread command handler
- [ ] `event_emitter.h` - Worker thread event API

### Component Integration

- [ ] Update `encode.h/cpp` for command/event handling
- [ ] Update `transport.h/cpp` for command/event handling
- [ ] Update `ndi_input.h/cpp` for command/event handling
- [ ] Update `ui.h/cpp` for event processing

### State Management

- [ ] `shared_state.h` - Atomic state definitions
- [ ] `state_manager.h/cpp` - State synchronization

### Error Handling

- [ ] `error_handler.h/cpp` - Centralized error handling
- [ ] `recovery_manager.h/cpp` - Automatic recovery logic

### Testing

- [ ] Unit tests for queue implementations
- [ ] Integration tests for command flows
- [ ] Stress tests for concurrent access
- [ ] Performance benchmarks

### Documentation

- [ ] API documentation for message passing
- [ ] Thread safety guidelines
- [ ] Troubleshooting guide
