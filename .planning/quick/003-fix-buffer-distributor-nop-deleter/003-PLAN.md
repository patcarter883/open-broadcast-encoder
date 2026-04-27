phase: quick-task
plan: 003
type: execute
wave: 1
depends_on: []
files-modified:
  - source/transport/buffer_distributor.cpp
autonomous: true
must_haves:
  truths:
    - "Buffer data is properly copied before GStreamer sample is unreferenced"
  artifacts:
    - path: "source/transport/buffer_distributor.cpp"
      provides: "Fixed distribute_legacy with deep copy"

---

## Task 1: Fix no-op deleter by deep-copying buffer data

- files: source/transport/buffer_distributor.cpp
- action: |
  In `distribute_legacy()` (line 91-98), replace the no-op deleter pattern
  `std::shared_ptr<uint8_t[]>(buf.buf_data, [](uint8_t*) {})` with a deep copy.

  Current code:
  ```cpp
  auto shared = std::make_shared<shared_buffer>();
  shared->data = std::shared_ptr<uint8_t[]>(buf.buf_data, [](uint8_t*) {});
  shared->size = buf.buf_size;
  shared->seq = buf.seq;
  shared->ts_ntp = buf.ts_ntp;
  distribute(shared);
  ```

  Replace with:
  ```cpp
  auto shared = std::make_shared<shared_buffer>();
  shared->data = std::shared_ptr<uint8_t[]>(
      std::make_unique<uint8_t[]>(buf.buf_size).release());
  std::copy(buf.buf_data, buf.buf_data + buf.buf_size, shared->data.get());
  shared->size = buf.buf_size;
  shared->seq = buf.seq;
  shared->ts_ntp = buf.ts_ntp;
  distribute(shared);
  ```

  This ensures the transported data is an independent copy that lives beyond
  the encoder's GStreamer buffer lifecycle, fixing the use-after-free in
  `pull_video_buffer()` which calls `gst_sample_unref(sample)` before the
  distributor reads the data.

  Add `#include <algorithm>` at the top of the file if not already present
  (it already is at line 2).

- verify: |
  1. Confirm `#include <algorithm>` exists in buffer_distributor.cpp
  2. Build: `cmake --build build`
 3. Confirm no `shared_ptr<uint8_t[]>` with no-op deleter pattern remains
     in buffer_distributor.cpp: `grep -n 'shared_ptr<uint8_t\[\]>' source/transport/buffer_distributor.cpp` should NOT show a lambda deleter
- done: |
  distribute_legacy creates an independent deep copy of buffer data via
  shared_ptr + make_unique, and the build passes without the no-op deleter pattern.
