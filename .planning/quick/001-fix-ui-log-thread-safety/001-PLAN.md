phase: quick
plan: 001
type: execute
wave: 1
depends_on: []
files-modified:
  - source/ui/ui.cpp
autonomous: true
requirements:
  - THREAD-SAFETY-UI
must_haves:
  truths:
    - "All log append functions in ui.cpp use Fl::lock()/Fl::unlock()/Fl::awake() around insert() calls"
  artifacts:
    - path: "source/ui/ui.cpp"
      provides: "Thread-safe transport_log_append, encode_log_append_cb, encode_log_append"
  key_links:
    - from: "transport/transport.cpp or encode/encode.cpp"
      to: "source/ui/ui.cpp"
      via: "log append calls from background GStreamer/RIST threads"

## Plan

### Task 1: Fix FLTK thread-safety in log append functions

**files:** `source/ui/ui.cpp`

**action:**
Uncomment/add the `Fl::lock()` → `Fl::unlock()` → `Fl::awake()` pattern around `insert()` calls in three functions:

1. **`transport_log_append()`** (lines 466-472): Uncomment the three lines:
   - `Fl::lock();` (currently `// Fl::lock();` on line 468)
   - `Fl::unlock();` (currently `// Fl::unlock();` on line 470)
   - `Fl::awake();` (currently `// Fl::awake();` on line 471)

2. **`encode_log_append_cb()`** (lines 474-477): Add the full pattern before/after the existing `insert()`:
   ```cpp
   Fl::lock();
   encode_log_display->insert(msg.c_str());
   Fl::unlock();
   Fl::awake();
   ```

3. **`encode_log_append()`** (lines 479-482): Add the full pattern before/after the existing `insert()`:
   ```cpp
   Fl::lock();
   encode_log_display->insert(msg.c_str());
   Fl::unlock();
   Fl::awake();
   ```

Use the exact pattern from AGENTS.md: `Fl::lock()` → update UI → `Fl::unlock()` → `Fl::awake()`.

**verify:**
```bash
cmake --build build -t format-fix
cmake --build build --target check 2>/dev/null || cmake --build build 2>&1 | head -20
```
Confirm build succeeds with no errors.

**done:**
All three log append functions contain `Fl::lock()` before `insert()` and `Fl::unlock()` + `Fl::awake()` after, building without errors.
