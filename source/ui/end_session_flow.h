// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// End Session is one operator action: it closes the session the encoder holds
// and, because closing a session that is being streamed must not pull the
// receiver out from under a running sender, it stops the encode as part of the
// same action. The button is therefore live whenever a session is held,
// streaming or not.
//
// The ORDER is what this describes. A sender that is still running must never
// have its session torn out from under it, so:
//
//   1. stop the encode and JOIN its worker,
//   2. cancel the uplink probe and join it,
//   3. deallocate the session.
//
// Step 3 runs only when step 1 reported a completed stop. A stop that did not
// finish -- the worker missed its deadline, or the teardown failed -- abandons
// the whole action and leaves the session held: the operator can press End
// Session again after the stream is down, whereas a session deallocated under a
// live sender leaves a receiver taken down mid-stream and nothing to point the
// encoder at.
//
// Header-only and free of FLTK/libRIST/GStreamer, so the enable rule, the
// two-click arm and the ordering are pinned by tests with no UI, no sender and
// no portal.
#pragma once

#include <cstddef>
#include <functional>

namespace end_session_flow
{

// How long the second click stays armed. The operator has to press twice inside
// this window, so one stray click on a live broadcast cannot end the session.
constexpr double arm_window_seconds = 5.0;

// The state the button's enablement is derived from. `encode_active` is
// deliberately NOT part of the rule: End Session stops the stream itself, so
// gating it on the encode would hide the one control that closes a session
// while the operator is streaming.
struct state
{
  bool session_held {false};
  bool encode_active {false};
};

inline auto enabled(const state& s) -> bool
{
  return s.session_held;
}

// The single confirmation the action accepts: the first click arms, and only a
// second click inside the window confirms. A click after the window re-arms
// instead of confirming, which is what makes the arm a guard rather than a
// formality.
class confirm
{
public:
  enum class answer
  {
    armed,
    confirmed
  };

  // `now` is a monotonic seconds value (Fl::now() in the UI).
  auto click(double now) -> answer
  {
    if (armed_ && now - armed_at_ <= arm_window_seconds) {
      disarm();
      return answer::confirmed;
    }
    armed_ = true;
    armed_at_ = now;
    return answer::armed;
  }

  void disarm() { armed_ = false; }

  [[nodiscard]] auto armed() const -> bool { return armed_; }

  // When the current arm was taken, so the caller can state how long is left.
  [[nodiscard]] auto armed_at() const -> double { return armed_at_; }

private:
  bool armed_ {false};
  double armed_at_ {0.0};
};

// What stopping the encode reported. `timed_out` stands apart from `failed`:
// the worker did not return inside its deadline, so the sender may still be
// writing and nothing downstream of it may be touched.
enum class stop_outcome
{
  stopped,
  nothing_running,
  failed,
  timed_out
};

inline auto stop_completed(stop_outcome outcome) -> bool
{
  return outcome == stop_outcome::stopped
      || outcome == stop_outcome::nothing_running;
}

// Why the action was abandoned, for the transport log. Only meaningful when
// stop_completed() is false.
inline auto stop_failure_text(stop_outcome outcome) -> const char*
{
  switch (outcome) {
    case stop_outcome::timed_out:
      return "the encoder did not stop within its deadline and may still be "
             "sending";
    case stop_outcome::failed:
      return "the encoder reported an error while stopping";
    case stop_outcome::stopped:
    case stop_outcome::nothing_running:
      break;
  }
  return "";
}

struct result
{
  stop_outcome stop {stop_outcome::nothing_running};
  bool probe_cancelled {false};
  bool deallocated {false};
};

// The one order the action may use. Each step is injected so the sequence is
// observable, and so the caller keeps the blocking work (the joins, the HTTP)
// on whatever thread it chose -- never the UI thread.
//
// `cancel_probe` returns true when a ramp was actually running and was
// cancelled: a partial ramp yields no figure, so the caller tells the operator
// the measurement was abandoned rather than leaving the pane to read as
// "measured".
inline auto run(const std::function<stop_outcome()>& stop_encode,
                const std::function<bool()>& cancel_probe,
                const std::function<void()>& deallocate) -> result
{
  result out;
  out.stop = stop_encode ? stop_encode() : stop_outcome::nothing_running;
  if (!stop_completed(out.stop)) {
    // Refused before anything downstream runs: the session stays held.
    return out;
  }
  out.probe_cancelled = cancel_probe ? cancel_probe() : false;
  if (deallocate) {
    deallocate();
    out.deallocated = true;
  }
  return out;
}

}  // namespace end_session_flow
