# Third-party notices — open-broadcast-encoder

First-party code in this repository is licensed AGPL-3.0-or-later (see
`LICENSE`). The following third-party components are vendored as submodules,
vendored in-tree, or linked at build/run time and retain their own licenses:

| Component | Where | License | Notes |
|-----------|-------|---------|-------|
| FLTK | `external/fltk` (submodule) | LGPL-2.0 **with static-linking exception** | the exception permits static linking without LGPL relink obligations — noted explicitly because this app links FLTK statically |
| rist-cpp | `external/rist-cpp` (submodule) | BSD-2-Clause | C++ wrapper around librist |
| librist | fetched/built by rist-cpp (ExternalProject) | BSD-2-Clause | |
| sdp-tools-cpp | `external/sdp-tools-cpp` (submodule) | see its repository | first-party companion library, licensed in its own repo |
| cpp-httplib | `external/httplib.h` (vendored header) | MIT | HTTP control-plane client |
| nlohmann-json | vcpkg | MIT | JSON for the control plane |
| Catch2 | vcpkg (test feature) | BSL-1.0 | tests only, not distributed |
| GStreamer | system package, dynamically linked | LGPL-2.1 | encode pipelines; dynamic linking, no LGPL relink obligation triggered |
| NDI SDK / NDI runtime | **not included** | proprietary (NewTek/Vizrt) | the encoder never redistributes `libndi`. Users install the official NDI runtime themselves; the encoder loads it dynamically at runtime and degrades gracefully with an install prompt when absent (decision recorded 2026-07-18 in the product repo's LICENSING_NOTES.md) |

Keep the license files of vendored components intact when updating them.
When a new dependency is added, add it to this table in the same commit.
