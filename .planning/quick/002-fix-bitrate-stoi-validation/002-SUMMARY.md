# Summary: Fix unsafe `std::stoi` calls with validation

## Status: Complete

## Changes Made

### Task 1: Added `safe_parse_int` utility (`source/lib/lib.h`)
- Added free function `safe_parse_int(const std::string& s, int default_val)` that wraps `std::stoi` in try/catch
- Validates that the entire string was consumed (no trailing characters)
- Returns `default_val` on any exception or parse failure
- No new includes needed — `<string>` and `<exception>` already present

### Task 2: Replaced 3 bitrate `std::stoi` calls
- `source/main.cpp:201` — `safe_parse_int(app.encode_config.bitrate, 4300)`
- `source/stats/stats.cpp:12` — `safe_parse_int(encode_config.bitrate, 4300)` + added `#include "lib/lib.h"`
- `source/stats/statistics_aggregator.cpp:149` — `safe_parse_int(config.bitrate, 4300)` + added `#include "lib/lib.h"`

### Task 3: Replaced `std::stoi` in dialog (`source/ui/add_output_dialog.cpp:160`)
- `safe_parse_int(m_streams_input->value(), 1)` — added `#include "lib/lib.h"`

## Verification
- Zero remaining `std::stoi` calls outside the utility function itself
- All 4 call sites replaced with `safe_parse_int`
- Include guards properly added to files that needed them
- Build cannot complete in this environment (incomplete build dir), but no syntax errors in edits
