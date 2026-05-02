# Quick Task Plan: Fix bitrate usage in stats.cpp (Tech Debt 6)

**Source:** Compile errors in `source/stats/stats.cpp` where `encode_config.bitrate` (type `int`) is used with `std::string` methods.

**Type:** Bug fix — two lines in one file, no scope widening.

---

## Plan 01

phase: quick
plan: 01
type: execute
wave: 1
depends_on: []
files-modified:
  - source/stats/stats.cpp
autonomous: true
must_haves:
  truths:
    - "Project compiles without errors"
    - "bitrate is used as int throughout stats.cpp"
  artifacts:
    - path: "source/stats/stats.cpp"
      provides: "Corrected bitrate usage — no string method calls on int"

---

### Task 1: Fix bitrate string method calls in stats.cpp

**type:** auto
**files:**
- `source/stats/stats.cpp`

**action:**
Fix two lines in `source/stats/stats.cpp` where `encode_config.bitrate` (declared as `int` in `source/lib/lib.h:68`) is incorrectly used with `std::string` methods:

1. Line 16: Replace `double maxBitrate = std::stoi(encode_config.bitrate);` with `double maxBitrate = static_cast<double>(encode_config.bitrate);`
2. Line 82: Replace `ui.encode_bitrate_output->value(encode_config.bitrate.c_str());` with `ui.encode_bitrate_output->value(std::to_string(encode_config.bitrate).c_str());`

Do NOT change the type of `encode_config.bitrate` — it is intentionally `int` as defined in `source/lib/lib.h`. Do not modify any other lines.

**verify:**
```bash
cmake --build build 2>&1 | grep -c "error:"
```
Expected: `0` errors (the two bitrate-related errors must be gone). Full build must succeed.

**done:**
Build succeeds with zero errors and `encode_config.bitrate` is no longer passed to `std::stoi` or have `.c_str()` called on it.
