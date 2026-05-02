phase: quick
plan: 007
type: execute
wave: 1
depends_on: []
files-modified:
  - source/lib/lib.h
  - source/ui/ui.cpp
  - source/transport/transport.cpp
autonomous: true
must_haves:
  truths:
    - "output_config has separate host and port fields accessible without URL parsing"
    - "transport.cpp uses host and port directly, no url class instantiation"
    - "UI callback populates host and port when address input changes"
  artifacts:
    - path: "source/lib/lib.h"
      provides: "output_config with host (string) and port (int) fields"
    - path: "source/ui/ui.cpp"
      provides: "address callback that parses into host + port"
    - path: "source/transport/transport.cpp"
      provides: "direct host/port usage, url.h removed"
  key_links:
    - from: "source/ui/ui.cpp"
      to: "source/lib/lib.h"
      via: "output_config host/port fields populated by callback"
    - from: "source/transport/transport.cpp"
      to: "source/lib/lib.h"
      via: "output_config host/port used in rist_output_url format"

# Task 1: struct + utility
- id: "007-task-1"
  type: auto
  files:
    - source/lib/lib.h
  action: >
    Add `host` (std::string, default "127.0.0.1") and `port` (int, default 5000) fields
    to the `output_config` struct in lib.h. Keep the existing `address` field as-is for
    UI compatibility — do not remove it.

    Add an inline helper function (outside the struct, at namespace scope) called
    `parse_address` that takes a `std::string` in "host:port" format and returns a
    pair or tuple of (host, port). Use `std::string::find(':')` to split, then
    `std::stoi` for the port. Handle edge cases: no colon (port=5000 default), empty
    host (use "127.0.0.1" default), non-numeric port (use 5000 default).

  verify: |
    Compile check — the struct and function must be syntactically valid:
    ```bash
    cmake --build build 2>&1 | head -30
    ```
    (Expected: configure step first if build dir is stale, then check for
    syntax errors in lib.h)

  done: >
    `output_config` has `host` and `port` fields with defaults "127.0.0.1" and 5000.
    `parse_address()` function exists and handles the "host:port" split correctly.

# Task 2: UI callback
- id: "007-task-2"
  type: auto
  depends_on:
    - "007-task-1"
  files:
    - source/ui/ui.cpp
  action: >
    Update `input_rist_address_cb()` in ui.cpp to call `parse_address()` on the
    address string after the existing assignment. Populate `output_config->host`
    and `output_config->port` from the parsed result. The existing line
    `output_config->address = input_rist_address->value();` stays unchanged
    (UI still writes to address field). After it, add the parse call:

    ```cpp
    auto [h, p] = parse_address(output_config->address);
    output_config->host = h;
    output_config->port = p;
    ```

    No changes to ui.h or ui.fld — the callback signature and widget references
    remain the same.

  verify: |
    Full build compiles cleanly:
    ```bash
    cmake --build build 2>&1
    ```
    No errors or warnings about the new fields or parse_address call.

  done: >
    When the user types an address in the UI, both `host` and `port` fields are
    populated from the parsed value. The address field still works for UI display.

# Task 3: transport.cpp — eliminate url parsing
- id: "007-task-3"
  type: auto
  depends_on:
    - "007-task-2"
  files:
    - source/transport/transport.cpp
  action: >
    In `transport.cpp`, replace the url-based URL construction with direct
    host/port usage. Remove the `#include "url/url.h"` line (no longer needed).
    Remove the `url url {std::format("rist://{}", output_c.address)};` line
    and replace the loop body with:

    ```cpp
    string rist_output_url = std::format(
        "rist://{}:{}?bandwidth={}&buffer-min={}&buffer-max={}&rtt-min={}&rtt-max={}&"
        "reorder-buffer={}&timing-mode=2",
        output_c.host,
        output_c.port + (2 * i),
        output_c.bandwidth,
        output_c.buffer_min,
        output_c.buffer_max,
        output_c.rtt_min,
        output_c.rtt_max,
        output_c.reorder_buffer);
    ```

    Remove `using homer6::url;` from the top of the file.

  verify: |
    Full build and check that url.h is no longer included:
    ```bash
    cmake --build build 2>&1
    grep -n 'url.h\|homer6::url' source/transport/transport.cpp && echo "FAIL: url.h still referenced" || echo "OK: url.h removed"
    ```

  done: >
    transport.cpp constructs RIST URLs using `output_c.host` and `output_c.port`
    directly. The url.h header is no longer included or used. The url class is
    eliminated from the transport module.
