# Quick Task Plan: Remove dead code (Tech Debt 1)

## Objective
Remove two unused `_cb` callback functions from the UI module and uncomment the thread-safety lock calls in `transport_log_append()`.

## Task 1: Remove dead `_cb` functions
**Files:**
- `source/ui/ui.h`
- `source/ui/ui.cpp`

**Action:**

1. **Remove `encode_log_append_cb()` declaration** from `source/ui/ui.h:78`:
   Delete the line: `void encode_log_append_cb(const std::string& msg) const;`

2. **Remove `transport_log_append_cb()` declaration** from `source/ui/ui.h:76`:
   Delete the line: `void transport_log_append_cb(const std::string& msg) const;`

3. **Remove `encode_log_append_cb()` definition** from `source/ui/ui.cpp:451-454`:
   Delete the entire function body:
   ```cpp
   void user_interface::encode_log_append_cb(const std::string& msg) const
   {
     encode_log_display->insert(msg.c_str());
   }
   ```

**Verify:**
```bash
# Verify declarations are removed
rg 'encode_log_append_cb' source/ui/ui.h && echo "FAIL: still in header" || echo "OK: removed from header"
rg 'transport_log_append_cb' source/ui/ui.h && echo "FAIL: still in header" || echo "OK: removed from header"

# Verify definition is removed
rg 'encode_log_append_cb' source/ui/ui.cpp && echo "FAIL: still defined" || echo "OK: removed definition"

# Verify the active functions remain intact
rg 'void encode_log_append\(' source/ui/ui.cpp && echo "OK: encode_log_append exists" || echo "FAIL: encode_log_append missing"
rg 'void transport_log_append\(' source/ui/ui.cpp && echo "OK: transport_log_append exists" || echo "FAIL: transport_log_append missing"

# Verify main.cpp still compiles (it calls encode_log_append, not encode_log_append_cb)
rg 'encode_log_append\(' source/main.cpp && echo "OK: main.cpp uses encode_log_append" || echo "FAIL"
```

## Task 2: Uncomment thread-safety locks in `transport_log_append()`
**Files:**
- `source/ui/ui.cpp`

**Action:**

1. **Uncomment lock calls** in `source/ui/ui.cpp:443-448`:
   Change from:
   ```cpp
   void user_interface::transport_log_append(const std::string& msg) const
   {
     // Fl::lock();
     transport_log_display->insert(msg.c_str());
     // Fl::unlock();
     // Fl::awake();
   }
   ```
   To:
   ```cpp
   void user_interface::transport_log_append(const std::string& msg) const
   {
     Fl::lock();
     transport_log_display->insert(msg.c_str());
     Fl::unlock();
     Fl::awake();
   }
   ```

**Verify:**
```bash
# Verify lock calls are uncommented
rg '// Fl::lock\(\)' source/ui/ui.cpp && echo "FAIL: still commented" || echo "OK: Fl::lock uncommented"
rg '// Fl::unlock\(\)' source/ui/ui.cpp && echo "FAIL: still commented" || echo "OK: Fl::unlock uncommented"
rg '// Fl::awake\(\)' source/ui/ui.cpp && echo "FAIL: still commented" || echo "OK: Fl::awake uncommented"

# Verify the active code structure
rg 'Fl::lock' source/ui/ui.cpp | head -3
rg 'Fl::unlock' source/ui/ui.cpp | head -3
```

## Scope Notes
- No SDP constant changes — left as-is for future feature
- No changes to ui.fld, ui.cxx, ui_func.cxx (FLTK-generated files)
- `encode_log_append_cb` is only referenced in ui.h/ui.cpp — no callers in main.cpp or other files
