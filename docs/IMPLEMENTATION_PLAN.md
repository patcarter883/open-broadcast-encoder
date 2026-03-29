# Implementation Plan: Bidirectional Message Passing

## Overview

This document outlines a 5-phase implementation plan for adding bidirectional message passing between the main thread (UI) and worker threads (encoder, transport, NDI) in the Open Broadcast Encoder.

**Estimated Timeline**: 6-8 weeks  
**Risk Level**: Medium (requires careful thread-safety testing)

---

## Phase 1: Foundation (Week 1)

### Goals
- Establish core message passing infrastructure
- Implement thread-safe queues
- Create message type definitions

### Tasks

#### 1.1 Message Types (2 days)
- [ ] Create `source/message/message.h`
  - Define `command`, `event`, `query` structs
  - Define enum classes for message types
  - Implement payload variant types

```cpp
// source/message/message.h
#pragma once

#include <variant>
#include <string>
#include <chrono>
#include <cstdint>

namespace message {

enum class command_type {
  encode_start,
  encode_stop,
  encode_pause,
  encode_set_bitrate,
  encode_set_resolution,
  transport_connect,
  transport_disconnect,
  ndi_refresh_sources,
  ndi_select_source,
  shutdown
};

enum class event_type {
  encode_started,
  encode_stopped,
  encode_error,
  encode_bitrate_changed,
  encode_stats_update,
  transport_connected,
  transport_disconnected,
  transport_stats_update,
  ndi_sources_updated,
  pipeline_state_changed,
  worker_error
};

struct command {
  command_type type;
  uint64_t command_id;
  std::chrono::steady_clock::time_point timestamp;
  std::variant<
    std::monostate,
    uint32_t,           // bitrate value
    std::string,        // source name
    struct resolution { uint32_t width; uint32_t height; }
  > payload;
};

struct event {
  event_type type;
  uint64_t correlation_id;  // Links to command_id if applicable
  std::chrono::steady_clock::time_point timestamp;
  std::variant<
    std::monostate,
    uint32_t,           // bitrate value
    std::string,        // error message
    struct stats { uint32_t quality; uint32_t bandwidth; }
  > payload;
};

} // namespace message
```

#### 1.2 Command Queue (2 days)
- [ ] Create `source/message/command_queue.h/.cpp`
  - Thread-safe producer-consumer queue
  - Condition variable for blocking wait
  - Timeout support for graceful shutdown

```cpp
// source/message/command_queue.h
#pragma once

#include "message.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <optional>
#include <chrono>

namespace message {

class command_queue {
public:
  command_queue();
  ~command_queue();
  
  // Producer (main thread)
  void push(command&& cmd);
  bool try_push(command&& cmd);
  
  // Consumer (worker thread)
  std::optional<command> pop();
  std::optional<command> pop_with_timeout(std::chrono::milliseconds timeout);
  
  // Shutdown
  void shutdown();
  bool is_shutdown() const;
  
  // Stats
  size_t size() const;
  bool empty() const;
  
private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<command> queue_;
  std::atomic_bool shutdown_{false};
};

} // namespace message
```

#### 1.3 Lock-Free Event Queue (2 days)
- [ ] Create `source/message/event_queue.h`
  - Single-producer, single-consumer lock-free queue
  - Power-of-2 capacity for efficient indexing
  - Cache-line alignment to prevent false sharing

```cpp
// source/message/event_queue.h
#pragma once

#include "message.h"
#include <array>
#include <atomic>
#include <new>

namespace message {

template<size_t Capacity>
class event_queue {
  static_assert((Capacity & (Capacity - 1)) == 0, 
                "Capacity must be power of 2");
  
public:
  static constexpr size_t capacity = Capacity;
  
  event_queue();
  
  // Producer (worker thread only)
  bool try_push(const event& evt);
  bool try_push(event&& evt);
  
  // Consumer (main thread only)
  bool try_pop(event& evt);
  
  bool empty() const;
  bool full() const;
  size_t size() const;
  
private:
  alignas(64) std::array<event, Capacity> buffer_;
  alignas(64) std::atomic<size_t> head_{0};
  alignas(64) std::atomic<size_t> tail_{0};
  
  static constexpr size_t mask_ = Capacity - 1;
};

// Type alias for common size
using default_event_queue = event_queue<1024>;

} // namespace message
```

### Deliverables
- [ ] `source/message/message.h` - Core message definitions
- [ ] `source/message/command_queue.h/.cpp` - Thread-safe command queue
- [ ] `source/message/event_queue.h` - Lock-free event queue
- [ ] Unit tests for queue implementations

### Success Criteria
- Command queue passes unit tests with multiple producers/consumers
- Event queue benchmarks show <100ns latency for push/pop
- Valgrind shows no race conditions or memory leaks

---

## Phase 2: Frontend API Layer (Week 2)

### Goals
- Create high-level API for UI code
- Implement command sender for main thread
- Implement event receiver for main thread

### Tasks

#### 2.1 Command Sender (2 days)
- [ ] Create `source/message/command_sender.h/.cpp`
  - High-level API for sending commands
  - Command ID generation and tracking
  - Optional: Future-based async results

```cpp
// source/message/command_sender.h
#pragma once

#include "command_queue.h"
#include <functional>
#include <future>
#include <unordered_map>

namespace message {

using command_callback = std::function<void(bool success, const std::string& msg)>;

class command_sender {
public:
  explicit command_sender(command_queue& queue);
  
  // Fire-and-forget commands
  uint64_t send(command_type type);
  uint64_t send(command_type type, command::payload_variant payload);
  
  // Commands with callback acknowledgment
  uint64_t send_with_callback(command_type type, command_callback callback);
  uint64_t send_with_callback(command_type type, 
                               command::payload_variant payload,
                               command_callback callback);
  
  // Handle acknowledgment from worker
  void handle_acknowledgment(uint64_t command_id, bool success, 
                             const std::string& message);
  
private:
  command_queue& queue_;
  std::atomic<uint64_t> next_id_{1};
  std::mutex callbacks_mutex_;
  std::unordered_map<uint64_t, command_callback> pending_callbacks_;
};

} // namespace message
```

#### 2.2 Event Receiver (2 days)
- [ ] Create `source/message/event_receiver.h/.cpp`
  - Event dispatcher with handler registration
  - Integration with FLTK's Fl::awake()
  - Batch event processing for efficiency

```cpp
// source/message/event_receiver.h
#pragma once

#include "event_queue.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace message {

using event_handler = std::function<void(const event&)>;

class event_receiver {
public:
  explicit event_receiver(default_event_queue& queue);
  
  // Handler registration
  void on(event_type type, event_handler handler);
  void once(event_type type, event_handler handler);
  void off(event_type type);
  
  // Process pending events (call from FLTK idle callback)
  void process_pending(size_t max_events = 100);
  
  // Check if events are available
  bool has_events() const;
  
  // Wake FLTK (called by worker threads via callback)
  void wake_ui();
  
  // Set the wake callback (used to call Fl::awake)
  void set_wake_callback(std::function<void()> callback);
  
private:
  default_event_queue& queue_;
  std::unordered_map<event_type, std::vector<event_handler>> handlers_;
  std::function<void()> wake_callback_;
  
  void dispatch(const event& evt);
};

} // namespace message
```

#### 2.3 Shared State (2 days)
- [ ] Create `source/message/shared_state.h`
  - Atomic state variables for fast access
  - Cache-line aligned to prevent false sharing

```cpp
// source/message/shared_state.h
#pragma once

#include <atomic>
#include <cstdint>

namespace message {

// Cache-line size (common for x86_64 and ARM64)
constexpr size_t cache_line_size = 64;

// Base template for cache-aligned atomic
template<typename T>
struct alignas(cache_line_size) aligned_atomic {
  std::atomic<T> value;
};

struct encode_state {
  aligned_atomic<uint32_t> current_bitrate_kbps{0};
  aligned_atomic<uint32_t> target_bitrate_kbps{0};
  aligned_atomic<bool> encoding_active{false};
  aligned_atomic<uint64_t> frames_encoded{0};
  aligned_atomic<uint64_t> bytes_encoded{0};
  // Padding to prevent false sharing with next state
  char padding[cache_line_size - sizeof(uint64_t)];
};

struct transport_state {
  aligned_atomic<uint32_t> bandwidth_kbps{0};
  aligned_atomic<uint32_t> quality{0};
  aligned_atomic<uint64_t> packets_sent{0};
  aligned_atomic<uint64_t> packets_retransmitted{0};
  char padding[cache_line_size - sizeof(uint64_t)];
};

// Global state instance (defined in shared_state.cpp)
extern encode_state g_encode_state;
extern transport_state g_transport_state;

} // namespace message
```

### Deliverables
- [ ] `source/message/command_sender.h/.cpp` - High-level command API
- [ ] `source/message/event_receiver.h/.cpp` - Event handling API
- [ ] `source/message/shared_state.h/.cpp` - Atomic shared state
- [ ] Integration tests for sender/receiver pairs

### Success Criteria
- Command sender can send 10,000 commands/second
- Event receiver can dispatch events with <1ms latency
- FLTK UI remains responsive during high event volume

---

## Phase 3: Event System (Week 3)

### Goals
- Implement event emitter for worker threads
- Add event generation to encode, transport, NDI
- Create error event handling

### Tasks

#### 3.1 Event Emitter (2 days)
- [ ] Create `source/message/event_emitter.h/.cpp`
  - Worker-side API for emitting events
  - Automatic Fl::awake() triggering
  - Event batching for high-frequency updates

```cpp
// source/message/event_emitter.h
#pragma once

#include "event_queue.h"
#include <functional>

namespace message {

class event_emitter {
public:
  explicit event_emitter(default_event_queue& queue);
  
  // Set callback to trigger UI wake (Fl::awake)
  void set_wake_callback(std::function<void()> callback);
  
  // Emit events
  bool emit(event_type type);
  bool emit(event_type type, event::payload_variant payload);
  bool emit(event_type type, uint64_t correlation_id);
  bool emit(event_type type, uint64_t correlation_id, 
            event::payload_variant payload);
  
  // Batch emit (for stats updates)
  template<typename Iterator>
  size_t emit_batch(Iterator begin, Iterator end);
  
  // Check if queue has space
  bool can_emit() const;
  
private:
  default_event_queue& queue_;
  std::function<void()> wake_callback_;
  
  void do_wake();
};

} // namespace message
```

#### 3.2 Command Processor (2 days)
- [ ] Create `source/message/command_processor.h/.cpp`
  - Base class for worker command handling
  - Acknowledgment generation
  - Error event conversion

```cpp
// source/message/command_processor.h
#pragma once

#include "command_queue.h"
#include "event_emitter.h"

namespace message {

class command_processor {
public:
  command_processor(command_queue& cmd_queue, event_emitter& evt_emitter);
  virtual ~command_processor() = default;
  
  // Main processing loop (run in worker thread)
  void run();
  void stop();
  
  // Override in derived classes
  virtual void handle_command(const command& cmd) = 0;
  virtual void on_shutdown() {}
  
protected:
  // Helper to send acknowledgment
  void acknowledge(uint64_t command_id, bool success, 
                   const std::string& message = {});
  
  // Helper to report error
  void report_error(const std::string& component, 
                    const std::string& message);
  
  event_emitter& emitter() { return emitter_; }
  
private:
  command_queue& queue_;
  event_emitter& emitter_;
  std::atomic_bool running_{false};
};

} // namespace message
```

#### 3.3 Error Events (2 days)
- [ ] Create `source/message/error_handler.h/.cpp`
  - Centralized error event generation
  - Error severity classification
  - Recovery suggestions

```cpp
// source/message/error_handler.h
#pragma once

#include "message.h"
#include <string>

namespace message {

enum class error_severity {
  info,      // Informational, no action needed
  warning,   // Warning, operation continues
  error,     // Error, operation may have failed
  critical   // Critical, system may be unstable
};

struct error_info {
  std::string component;
  std::string message;
  error_severity severity;
  std::chrono::steady_clock::time_point timestamp;
  std::optional<uint64_t> related_command_id;
  std::optional<std::string> recovery_suggestion;
};

class error_handler {
public:
  explicit error_handler(event_emitter& emitter);
  
  void report(const error_info& error);
  
  // Convenience methods
  void warning(const std::string& component, const std::string& message);
  void error(const std::string& component, const std::string& message);
  void critical(const std::string& component, const std::string& message);
  
private:
  event_emitter& emitter_;
};

} // namespace message
```

### Deliverables
- [ ] `source/message/event_emitter.h/.cpp` - Worker event API
- [ ] `source/message/command_processor.h/.cpp` - Worker command processing
- [ ] `source/message/error_handler.h/.cpp` - Error event handling
- [ ] Update CMakeLists.txt to include message module

### Success Criteria
- Event emitter can emit 50,000 events/second
- Command processor handles 1,000 commands/second
- Error events contain actionable recovery suggestions

---

## Phase 4: Component Integration (Weeks 4-5)

### Goals
- Integrate message passing into encode, transport, NDI
- Replace existing callbacks with event system
- Ensure backward compatibility

### Tasks

#### 4.1 Encode Integration (3 days)
- [ ] Modify `source/encode/encode.h`
  - Inherit from `command_processor`
  - Add event emission points
  - Keep existing API for backward compatibility

```cpp
// Changes to source/encode/encode.h
#pragma once

// Add to includes
#include "message/command_processor.h"
#include "message/event_emitter.h"

// Change class declaration
class encode : public message::command_processor {
public:
  // Existing constructor
  encode(library* lib);
  
  // New: Message passing constructor
  encode(library* lib, message::command_queue& cmd_queue, 
         message::event_emitter& evt_emitter);
  
  // Override command handling
  void handle_command(const message::command& cmd) override;
  
  // Existing methods remain for backward compatibility
  void play_pipeline();
  void stop_pipeline();
  void set_encode_bitrate(uint32_t bitrate);
  
private:
  void handle_start(const message::command& cmd);
  void handle_stop(const message::command& cmd);
  void handle_set_bitrate(const message::command& cmd);
  
  // Emit state change events
  void emit_state_change(gst_state old_state, gst_state new_state);
  void emit_stats_update();
  void emit_error(const std::string& message);
  
  library* lib_;
  bool using_message_passing_ = false;
};
```

- [ ] Modify `source/encode/encode.cpp`
  - Implement command handlers
  - Add event emission in pipeline callbacks
  - Wire up GStreamer bus messages to events

#### 4.2 Transport Integration (2 days)
- [ ] Modify `source/transport/transport.h/cpp`
  - Add command handling for connect/disconnect
  - Emit stats events from RIST callback
  - Replace direct UI callback with event emission

#### 4.3 NDI Integration (2 days)
- [ ] Modify `source/ndi_input/ndi_input.h/cpp`
  - Add command handling for source selection
  - Emit source discovery events
  - Replace direct UI callback with event emission

#### 4.4 UI Integration (3 days)
- [ ] Modify `source/ui/ui.h/cpp`
  - Add event receiver member
  - Register event handlers
  - Replace polling with event-driven updates

```cpp
// Changes to source/ui/ui.h
#pragma once

#include "message/event_receiver.h"
#include "message/command_sender.h"

class user_interface {
public:
  // Existing methods...
  
  // New: Initialize message passing
  void init_message_passing(message::command_queue& encode_cmd_queue,
                            message::default_event_queue& event_queue);
  
  // Called from FLTK idle callback
  void process_events();
  
private:
  // Message passing components
  std::unique_ptr<message::command_sender> cmd_sender_;
  std::unique_ptr<message::event_receiver> evt_receiver_;
  
  // Event handlers
  void on_encode_started(const message::event& evt);
  void on_encode_stopped(const message::event& evt);
  void on_encode_error(const message::event& evt);
  void on_transport_stats(const message::event& evt);
  void on_ndi_sources_updated(const message::event& evt);
  
  // Existing members...
};
```

#### 4.5 Main Integration (2 days)
- [ ] Modify `source/main.cpp`
  - Create message queues
  - Wire up components
  - Start worker threads with message passing

```cpp
// Changes to source/main.cpp

#include "message/command_queue.h"
#include "message/event_queue.h"
#include "message/shared_state.h"

// Global message infrastructure
std::unique_ptr<message::command_queue> g_encode_cmd_queue;
std::unique_ptr<message::command_queue> g_transport_cmd_queue;
message::default_event_queue g_event_queue;

int main(int argc, char** argv) {
  // Initialize message queues
  g_encode_cmd_queue = std::make_unique<message::command_queue>();
  g_transport_cmd_queue = std::make_unique<message::command_queue>();
  
  // Initialize shared state
  message::g_encode_state = {};
  message::g_transport_state = {};
  
  // Create event emitter with Fl::awake callback
  message::event_emitter emitter(g_event_queue);
  emitter.set_wake_callback([]() { Fl::awake(); });
  
  // Initialize UI with message passing
  ui.init_message_passing(*g_encode_cmd_queue, g_event_queue);
  
  // Create encoder with message passing
  auto encoder = std::make_unique<encode>(&app, 
                                          *g_encode_cmd_queue, 
                                          emitter);
  
  // Start encoder thread
  std::thread encode_thread([&encoder]() {
    encoder->run();  // Runs command processing loop
  });
  
  // Run FLTK event loop
  Fl::run();
  
  // Shutdown
  g_encode_cmd_queue->shutdown();
  encode_thread.join();
  
  return 0;
}
```

### Deliverables
- [ ] Updated `source/encode/encode.h/.cpp`
- [ ] Updated `source/transport/transport.h/.cpp`
- [ ] Updated `source/ndi_input/ndi_input.h/.cpp`
- [ ] Updated `source/ui/ui.h/.cpp`
- [ ] Updated `source/main.cpp`

### Success Criteria
- All existing functionality works with message passing
- UI updates are event-driven (no polling)
- Thread sanitizer shows no data races
- Performance is equal or better than callback approach

---

## Phase 5: Testing & Polish (Weeks 6-8)

### Goals
- Comprehensive testing
- Performance optimization
- Documentation

### Tasks

#### 5.1 Unit Tests (3 days)
- [ ] Create `tests/message/test_command_queue.cpp`
- [ ] Create `tests/message/test_event_queue.cpp`
- [ ] Create `tests/message/test_sender_receiver.cpp`

```cpp
// tests/message/test_command_queue.cpp
#include <gtest/gtest.h>
#include "message/command_queue.h"
#include <thread>
#include <vector>
#include <atomic>

using namespace message;

TEST(CommandQueue, SingleThreadedPushPop) {
  command_queue queue;
  
  command cmd{command_type::encode_start, 1, {}, {}};
  queue.push(std::move(cmd));
  
  auto result = queue.pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->type, command_type::encode_start);
}

TEST(CommandQueue, MultiThreadedConcurrency) {
  command_queue queue;
  std::atomic<uint64_t> pushed{0};
  std::atomic<uint64_t> popped{0};
  
  std::vector<std::thread> producers;
  for (int i = 0; i < 4; ++i) {
    producers.emplace_back([&]() {
      for (int j = 0; j < 1000; ++j) {
        command cmd{command_type::encode_start, 
                    static_cast<uint64_t>(j), {}, {}};
        queue.push(std::move(cmd));
        ++pushed;
      }
    });
  }
  
  std::thread consumer([&]() {
    while (popped < 4000) {
      if (auto cmd = queue.pop_with_timeout(std::chrono::milliseconds(100))) {
        ++popped;
      }
    }
  });
  
  for (auto& t : producers) t.join();
  consumer.join();
  
  EXPECT_EQ(pushed.load(), 4000);
  EXPECT_EQ(popped.load(), 4000);
}

TEST(CommandQueue, ShutdownBehavior) {
  command_queue queue;
  queue.shutdown();
  
  command cmd{command_type::encode_start, 1, {}, {}};
  EXPECT_THROW(queue.push(std::move(cmd)), std::runtime_error);
  
  auto result = queue.pop();
  EXPECT_FALSE(result.has_value());
}
```

#### 5.2 Integration Tests (3 days)
- [ ] Create `tests/integration/test_encode_commands.cpp`
- [ ] Create `tests/integration/test_transport_events.cpp`
- [ ] Create `tests/integration/test_full_pipeline.cpp`

#### 5.3 Stress Tests (2 days)
- [ ] High-frequency command test (10k commands/sec)
- [ ] High-frequency event test (50k events/sec)
- [ ] Memory leak test (24-hour run)
- [ ] Thread contention test

```cpp
// tests/stress/test_high_frequency.cpp
TEST(StressTest, HighFrequencyEvents) {
  message::default_event_queue queue;
  message::event_emitter emitter(queue);
  message::event_receiver receiver(queue);
  
  std::atomic<size_t> received{0};
  receiver.on(event_type::encode_stats_update, 
              [&](const event&) { ++received; });
  
  auto start = std::chrono::steady_clock::now();
  
  // Emit 100k events as fast as possible
  for (int i = 0; i < 100000; ++i) {
    emitter.emit(event_type::encode_stats_update);
  }
  
  // Process all events
  while (received < 100000) {
    receiver.process_pending(1000);
  }
  
  auto elapsed = std::chrono::steady_clock::now() - start;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  
  std::cout << "Processed 100k events in " << ms << "ms ("
            << (100000.0 / ms * 1000) << " events/sec)" << std::endl;
  
  EXPECT_EQ(received.load(), 100000);
}
```

#### 5.4 Performance Optimization (3 days)
- [ ] Profile message passing overhead
- [ ] Optimize hot paths
- [ ] Consider memory pool for events
- [ ] Batch high-frequency events (stats)

#### 5.5 Documentation (3 days)
- [ ] API documentation (Doxygen)
- [ ] Thread safety guidelines
- [ ] Migration guide from callbacks
- [ ] Troubleshooting guide

### Deliverables
- [ ] Comprehensive test suite
- [ ] Performance benchmarks
- [ ] Complete documentation
- [ ] Migration guide

### Success Criteria
- 100% unit test coverage for message module
- Zero memory leaks (Valgrind clean)
- No data races (ThreadSanitizer clean)
- <1% performance overhead vs callbacks
- All documentation complete

---

## Appendices

### Appendix A: File Checklist

#### New Files (Phase 1-3)
```
source/message/
├── message.h
├── command_queue.h
├── command_queue.cpp
├── event_queue.h
├── command_sender.h
├── command_sender.cpp
├── event_receiver.h
├── event_receiver.cpp
├── shared_state.h
├── shared_state.cpp
├── event_emitter.h
├── event_emitter.cpp
├── command_processor.h
├── command_processor.cpp
└── error_handler.h
└── error_handler.cpp
```

#### Modified Files (Phase 4)
```
source/encode/encode.h        (modify)
source/encode/encode.cpp      (modify)
source/transport/transport.h  (modify)
source/transport/transport.cpp(modify)
source/ndi_input/ndi_input.h  (modify)
source/ndi_input/ndi_input.cpp(modify)
source/ui/ui.h                (modify)
source/ui/ui.cpp              (modify)
source/main.cpp               (modify)
CMakeLists.txt                (modify)
```

#### Test Files (Phase 5)
```
tests/message/
├── test_command_queue.cpp
├── test_event_queue.cpp
└── test_sender_receiver.cpp

tests/integration/
├── test_encode_commands.cpp
├── test_transport_events.cpp
└── test_full_pipeline.cpp

tests/stress/
├── test_high_frequency.cpp
└── test_memory_leaks.cpp
```

### Appendix B: Testing Scenarios

#### Scenario 1: Command Acknowledgment
1. UI sends `encode_start` command
2. Worker receives and processes command
3. Worker sends acknowledgment event
4. UI receives acknowledgment and updates state

#### Scenario 2: Concurrent Commands
1. UI sends 100 commands rapidly
2. Worker processes commands in order
3. All commands acknowledged
4. No commands dropped

#### Scenario 3: Event Flooding
1. Worker generates 10k events/sec
2. UI processes events without blocking
3. Event queue never overflows
4. UI remains responsive

#### Scenario 4: Error Recovery
1. Worker encounters error
2. Error event sent to UI
3. UI displays error dialog
4. User initiates recovery
5. Recovery command sent
6. Worker recovers and resumes

#### Scenario 5: Graceful Shutdown
1. User closes application
2. Shutdown command sent to all workers
3. Workers flush pending events
4. Workers exit cleanly
5. No memory leaks

#### Scenario 6: Thread Safety
1. Multiple threads send commands
2. Multiple threads emit events
3. No data races detected
4. State remains consistent

### Appendix C: Common Pitfalls

#### Pitfall 1: Missing Fl::awake()
```cpp
// WRONG - UI will freeze
void worker_thread() {
  event_queue_.try_push(evt);
  // Missing Fl::awake()!
}

// CORRECT
void worker_thread() {
  if (event_queue_.try_push(evt)) {
    Fl::awake();  // Wake UI thread
  }
}
```

#### Pitfall 2: Blocking in Event Handler
```cpp
// WRONG - Blocks FLTK event loop
void on_event(const event& evt) {
  std::this_thread::sleep_for(std::chrono::seconds(5));
  update_ui();
}

// CORRECT - Defer heavy work
void on_event(const event& evt) {
  update_ui();  // Fast UI update
  
  // Defer heavy work
  std::thread([this]() {
    do_heavy_work();
    Fl::awake();
  }).detach();
}
```

#### Pitfall 3: Non-Atomic State Access
```cpp
// WRONG - Data race
struct state {
  int value;  // Not atomic!
};

// CORRECT - Atomic access
struct state {
  std::atomic<int> value;
};
```

#### Pitfall 4: Queue Overflow
```cpp
// WRONG - Silent event loss
void emit_event(const event& evt) {
  queue_.try_push(evt);  // May fail silently
}

// CORRECT - Handle overflow
void emit_event(const event& evt) {
  if (!queue_.try_push(evt)) {
    // Log warning, consider dropping or blocking
    log_warning("Event queue full, dropping event");
  }
}
```

#### Pitfall 5: Deadlock with FLTK Lock
```cpp
// WRONG - Deadlock
void worker_thread() {
  Fl::lock();           // Lock FLTK
  queue_.push(evt);     // May block, holding lock
  Fl::unlock();
}

// CORRECT - Minimize lock scope
void worker_thread() {
  // Prepare event without lock
  event evt = prepare_event();
  
  // Lock only for UI update
  Fl::lock();
  // Update UI...
  Fl::unlock();
  Fl::awake();
}
```

#### Pitfall 6: Callback Lifetime
```cpp
// WRONG - Callback may outlive captured objects
void setup_handler() {
  std::string local = "data";
  receiver_.on(event_type::x, [&local](const event&) {
    use(local);  // Undefined behavior if receiver outlives setup
  });
}

// CORRECT - Use shared_ptr or weak_ptr
void setup_handler() {
  auto data = std::make_shared<std::string>("data");
  receiver_.on(event_type::x, [data](const event&) {
    use(*data);  // Safe, shared_ptr extends lifetime
  });
}
```

---

## Timeline Summary

| Phase | Duration | Key Deliverables |
|-------|----------|------------------|
| 1: Foundation | Week 1 | Message types, queues |
| 2: Frontend API | Week 2 | Sender, receiver, shared state |
| 3: Event System | Week 3 | Emitter, processor, error handling |
| 4: Integration | Weeks 4-5 | Component integration |
| 5: Testing | Weeks 6-8 | Tests, docs, polish |

**Total: 6-8 weeks**
