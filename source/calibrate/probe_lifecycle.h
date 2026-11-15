// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The upload ramp's lifecycle: one probe at a time, held in a thread the encoder
// can always stop and join. The ramp drives the live RIST sender and runs for
// seconds, so every path that closes or replaces that sender -- End Session, a
// Start, Stop, shutdown -- has to be able to abort it and wait for it to be
// gone before doing anything else.
//
// The join is the important half. A ramp that is only asked to stop keeps
// writing to a sender the caller is about to destroy, and its completion
// callback (the measured figure, the pane, the settings save) then lands after
// the session it belonged to is gone. cancel_then() therefore orders the two:
// cancel and JOIN the ramp, then run the caller's close.
//
// Header-only and free of libRIST/GStreamer/FLTK so the ordering is pinned by a
// test with no sender and no UI.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace probe_lifecycle
{

// One ramp, and the thread running it.
//
// `stop()` blocks until the ramp's thread has returned, so after it the caller
// may destroy whatever the ramp was driving. It is idempotent and safe from any
// thread except the ramp's own (a thread must not join itself; that case is
// detected and skipped rather than deadlocking the UI).
class worker
{
public:
  // The ramp's body. It must poll cancel_requested() and return promptly when
  // it reads true -- that is what makes stop() a bounded wait rather than a
  // block until the plan finishes.
  using body = std::function<void()>;

  worker() = default;
  worker(const worker&) = delete;
  auto operator=(const worker&) -> worker& = delete;
  worker(worker&&) = delete;
  auto operator=(worker&&) -> worker& = delete;

  ~worker() { stop(); }

  // Starts `work` unless a ramp is already in flight, in which case nothing is
  // started and false is returned (the caller keeps its own status line). A
  // ramp that has finished but not been joined is reclaimed here.
  auto start(body work) -> bool
  {
    if (work == nullptr) {
      return false;
    }
    std::lock_guard<std::mutex> guard(mtx_);
    if (running_.load(std::memory_order_acquire)) {
      return false;
    }
    if (thread_.joinable()) {
      // Finished; the handle is still joinable until it is reaped.
      thread_.join();
    }
    cancel_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    try {
      thread_ = std::thread(
          [this, work = std::move(work)]
          {
            // Cleared on every way out of the ramp, including an exception, so
            // a failed run never leaves the worker looking busy.
            struct running_guard
            {
              std::atomic<bool>& flag;
              ~running_guard() { flag.store(false, std::memory_order_release); }
            };
            [[maybe_unused]] running_guard guard {running_};
            work();
          });
    } catch (...) {
      running_.store(false, std::memory_order_release);
      throw;
    }
    return true;
  }

  // Aborts the ramp and waits for its thread to return. Safe to call when
  // nothing is running, and safe to call twice.
  void stop()
  {
    cancel_.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> guard(mtx_);
    if (thread_.joinable()
        && thread_.get_id() == std::this_thread::get_id()) {
      // The ramp asking to stop itself: joining would wait on this very thread.
      // The flag is already set, so the ramp unwinds by its own next poll.
      return;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    cancel_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
  }

  // True while the ramp's body has not yet returned.
  [[nodiscard]] auto running() const -> bool
  {
    return running_.load(std::memory_order_acquire);
  }

  // What a ramp polls. True once stop() (or cancel_then()) has been called for
  // the run in flight.
  [[nodiscard]] auto cancel_requested() const -> bool
  {
    return cancel_.load(std::memory_order_acquire);
  }

private:
  mutable std::mutex mtx_;
  std::thread thread_;
  std::atomic<bool> cancel_ {false};
  std::atomic<bool> running_ {false};
};

// The order every path out of a session must follow: cancel and join the ramp
// FIRST, then close. `close` runs exactly once, after the ramp's thread is
// gone, so nothing the ramp does can touch a session that has already been
// deallocated. Returns true when a ramp was in flight and was cancelled -- the
// caller states that in the log, because a cancelled ramp yields no figure and
// the operator should know the measurement was abandoned rather than kept.
inline auto cancel_then(worker& probe, const std::function<void()>& close)
    -> bool
{
  const bool was_running = probe.running();
  probe.stop();
  if (close != nullptr) {
    close();
  }
  return was_running;
}

}  // namespace probe_lifecycle
