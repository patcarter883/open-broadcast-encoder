# Plan: Fix unsafe `std::stoi` calls with validation

## Goal
Replace 4 unsafestd::stoi calls with a `safe_parse_int` utility function that validates input and falls back to defaults.

## Tasks (3)

### 1. Add `safe_parse_int` to `source/lib/lib.h`
- Add `#include <string>` and `#include <stdexcept>` guards (check if already present, likely yes)
- Implement `int safe_parse_int(const std::string& s, int default_val)` wrapping `std::stoi` in try/catch for `std::invalid_argument` and `std::out_of_range`, returning `default_val` on failure
- Keep it as a plain free function (not a class method) per design decision

### 2. Replace `std::stoi` calls in 3 bitrate locations
- `source/main.cpp:201` — `std::stoi(app.encode_config.bitrate)` → `safe_parse_int(app.encode_config.bitrate, 4300)`
- `source/stats/stats.cpp:11` — `std::stoi(encode_config.bitrate)` → `safe_parse_int(encode_config.bitrate, 4300)`
- `source/stats/statistics_aggregator.cpp:148` — `std::stoi(config.bitrate)` → `safe_parse_int(config.bitrate, 4300)`

### 3. Replace `std::stoi` call in `source/ui/add_output_dialog.cpp`
- Line 159 — `std::stoi(m_streams_input->value())` → `safe_parse_int(m_streams_input->value(), 1)`

## Verification
- `cmake --build build -t format-fix` before committing
- Check all 4 locations are replaced
- Build succeeds with no new warnings
