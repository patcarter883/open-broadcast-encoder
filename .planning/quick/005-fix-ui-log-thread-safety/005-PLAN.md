# Quick Task Plan: Fix UI log append thread safety (Tech Debt 5)

## Objective

Add thread-safety locking to `encode_log_append()` so it matches the pattern used by `transport_log_append()`, preventing undefined behavior from background threads modifying FLTK widget state without locking.

## Problem

`encode_log_append()` at `source/ui/ui.cpp:457-460` is called from background threads (RIST callback, encode callback) but modifies `Fl_Text_Display` without calling `Fl::lock()` / `Fl::unlock()` / `Fl::awake()`. This is undefined behavior — FLTK is not thread-safe and can crash, corrupt display, or freeze the UI.

`transport_log_append()` at `source/ui/ui.cpp:449-455` already has the correct pattern and can be used as reference.

## Plan

### Task 1: Add Fl::lock/unlock/awake to `encode_log_append()`

**Action:** Modify `source/ui/ui.cpp` line 457-460 to add thread-safety locking around the `insert()` call.

**Files:** `source/ui/ui.cpp`

**Change:**
```cpp
// Before:
void user_interface::encode_log_append(const std::string& msg) const
{
  encode_log_display->insert(msg.c_str());
}

// After:
void user_interface::encode_log_append(const std::string& msg) const
{
  Fl::lock();
  encode_log_display->insert(msg.c_str());
  Fl::unlock();
  Fl::awake();
}
```

Note: We use the global `Fl::lock()`/`Fl::unlock()` functions (not the `this->lock()`/`this->unlock()` member functions) because `encode_log_append()` is a `const` method and the member functions are non-const.

**Verify:**
- File compiles without errors
- Pattern matches `transport_log_append()` exactly
- No other changes to `source/ui/ui.cpp`
