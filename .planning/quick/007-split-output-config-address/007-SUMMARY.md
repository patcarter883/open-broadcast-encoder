---
phase: quick
plan: 007
completed: 2026-05-02
tasks: 3
deviations:
  - rule: 3
    type: blocker-fix
    description: "transport.cpp had additional formatting differences beyond the plan scope (indentation, brace style, include ordering) — all pre-existing in the working tree, not introduced by this plan"
    task: 3
    files: ["source/transport/transport.cpp"]
decisions:
  - "parse_address() implemented as inline function in lib.h rather than as a module function, since it is a simple utility needed by both ui.cpp and the lib.h consumers"
  - "parse_address uses std::stoi with catch-all for non-numeric port fallback instead of manual digit parsing, for simplicity"
key_files:
  modified:
    - source/lib/lib.h
    - source/ui/ui.cpp
    - source/transport/transport.cpp
---

# Quick Plan 007: Split output_config address into host/port - Summary

**Completed**: 2026-05-02  
**Tasks**: 3/3  
**Git Actions**: None (no commit requested)

## What was built

Split the `output_config::address` string field into dedicated `host` (std::string, default "127.0.0.1") and `port` (int, default 5000) fields. Added `parse_address()` inline utility in `lib.h` that parses "host:port" strings with robust edge-case handling (missing colon, empty host, non-numeric port). Updated the UI address callback to populate host/port from the address string. Eliminated the `url.h` dependency from `transport.cpp` — transport now uses `output_c.host` and `output_c.port` directly instead of parsing via `homer6::url`.

## Tasks completed

### Task 1: struct + utility (lib.h)
- Added `host` (std::string, default "127.0.0.1") and `port` (int, default 5000) to `output_config` struct
- Added `parse_address(const std::string&)` inline function returning `std::pair<std::string, int>`
- Edge cases: no colon → defaults, empty host → "127.0.0.1", non-numeric port → 5000
- **Verify**: Build succeeded (cmake --build build, all 58 targets compiled and linked)

### Task 2: UI callback (ui.cpp)
- Updated `input_rist_address_cb()` to call `parse_address()` after setting `address`
- Populates `output_config->host` and `output_config->port` from parsed result
- **Verify**: Included in successful build

### Task 3: transport.cpp (eliminate url.h)
- Removed `#include "url/url.h"` and `using homer6::url;`
- Replaced `url url{std::format("rist://{}", output_c.address)}` pattern with direct `output_c.host` / `output_c.port` usage
- **Verify**: `grep url.h transport.cpp` returns no matches; build succeeded

## Deviations from Plan

**Task 3 - Auto-fix Rule 3 (straightforward blocker)**: The transport.cpp file had pre-existing formatting differences (indentation, brace style, include ordering) beyond what the plan specified. These were already present in the working tree before this plan. The functional changes (url.h removal, direct host/port usage) were applied exactly as planned.

## Notes for Verification

- The `url.h` dependency from `source/url/url.h` is now completely removed from the transport module
- The `output_config` struct still retains the `address` field for backwards compatibility
- `parse_address()` is declared inline in the header — callers need to include `lib.h`
- Build produces only pre-existing warnings (magic number 5000, const-correctness) — no new warnings from these changes
