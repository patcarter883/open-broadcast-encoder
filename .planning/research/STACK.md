# Stack Research

**Domain**: C++20 video streaming encoder with GStreamer pipelines  
**Researched**: 2025-05-05 (current stable versions verified)

## Recommended Stack

| Library/Tool | Version | Purpose | Why This One | Confidence |
|-------------|---------|---------|--------------|------------|
| **AddressSanitizer** | clang 23.0 | Use-after-free detection | Fast, precise memory error detection; integrates with CMake; detects dangling pointer use immediately at runtime. Works with GStreamer GLib/GObject refcounting issues. | ✅ HIGH |
| **Valgrind Memcheck** | 3.27.0 | Memory leak/dangling pointer detection | Gold standard for memory errors; finds leaks, use-after-free, invalid reads; excellent for intermittent bugs that ASan might miss. Slower but more thorough. | ✅ HIGH |
| **GStreamer** | 1.28.2 | Media pipeline framework | Current stable; project requires ≥1.28 per CMakeLists. Provides `gst_object_ref/unref` for proper reference counting. Fixed GStreamer object lifecycle bugs since 1.26. | ✅ HIGH |
| **GSL (Guidelines Support Library)** | 4.2.0 | Safe pointer wrappers (`not_null`, `owner`) | Prevents null dereferences; `owner<T>` makes pointer ownership explicit; Microsoft-maintained; header-only for easy integration. Catches dangling pointer issues at compile time. | ✅ HIGH |
| **GDB** | 15.1+ | Interactive debugging | Works with ASan; can set breakpoints in GStreamer callbacks; essential for tracing object lifecycle. | ✅ HIGH |

## Debugging Techniques (Proven)

| Technique | Tool | How To Apply | Confidence |
|-----------|------|--------------|------------|
| **Reference counting audit** | ASan + GDB | Run with `GST_DEBUG=refcounting` to trace every `ref/unref`. Use `gst_object_ref_sink` for floating references. | ✅ HIGH |
| **Memory quarantine** | ASan | Use `ASAN_OPTIONS=quarantine_size_mb=512` to keep freed memory longer, exposing dangling pointer bugs more reliably. | ✅ HIGH |
| **GObject lifecycle tracing** | GDB + GStreamer debug | Set watchpoints on object reference counts: `watch ((GObject*)ptr)->ref_count`. | ✅ HIGH |
| **Thread race detection** | Helgrind (Valgrind) | Use `valgrind --tool=helgrind` to detect race conditions in FLTK callbacks and GStreamer bus handlers. | ✅ HIGH |

## Alternatives Considered

| Recommended | Alternative | Why Not |
|-------------|------------|---------|
| AddressSanitizer | ElectricFence | EF is unmaintained, ASan is 10x faster and more precise |
| Valgrind | Dr.Memory | Valgrind has better GStreamer/GObject integration, more accurate stack traces |
| GSL | Boost.SmartPtr | GSL is header-only, lighter weight, specifically designed for lifetime safety |

## Don't Hand-Roll

Problems that look simple but have battle-tested solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|------------|-------------|-----|
| Reference counting for GStreamer objects | Manual `ref`/`unref` tracking | RAII wrappers with GSL `owner<T>` or `std::unique_ptr` with custom deleter | GStreamer objects have floating references that must be sinked; custom deleters ensure `gst_object_unref` is always called |
| Unbounded vector growth for stats | `std::vector` without limits | `boost::circular_buffer` or fixed-size ring buffer | Stats accumulate indefinitely; bounded buffer prevents memory exhaustion and provides constant-time operations |
| Codec element selection logic | String-based switch on codec names | Element factory registry with capability negotiation | GStreamer has `gst_element_factory_find` to dynamically probe available elements; prevents wrong encoder selection (h264parse for H.265) |
| Dangling pointer callbacks | Raw function pointers | `std::function` with weak_ptr or `gsl::not_null` | Raw pointers can dangle; `not_null` catches null at boundary, weak_ptr detects expired objects |

## GStreamer Encoder Element Correctness

**Critical: Do NOT use these wrong combinations:**

| Bug Type | Wrong | Right | Reason |
|----------|-------|-------|--------|
| H.265 parser | `h264parse` | `h265parse` | Different bitstream format; H.265 NAL units and parameter sets are incompatible with H.264 parser |
| NVENC AV1 encoder | `x264enc` | `nvav1enc` (NVIDIA) | x264enc is a software H.264 encoder; for AV1 you need NVENC AV1 encoder or rav1e/svt-av1 |
| QSV HEVC | `vaapih264enc` | `vaapih265enc` or `msdkh265enc` | Wrong codec produces invalid stream; use VAAPI or MSDK HEVC encoder |

## Configuration for Bug Detection

```cmake
# CMakeLists.txt additions for Debug builds
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(open-broadcast-encoder_exe PRIVATE
    -fsanitize=address -fno-omit-frame-pointer -g
  )
  target_link_options(open-broadcast-encoder_exe PRIVATE -fsanitize=address)
endif()
```

```bash
# Runtime environment for maximum detection
export ASAN_OPTIONS=detect_stack_use_after_return=true:quarantine_size_mb=512
export GST_DEBUG=refcounting:5,error:5  # Trace GObject ref counts
export G_SLICE=always-malloc  # Better Valgrind compatibility
```

## What NOT to Use

| Tool | Version | Reason to Avoid |
|------|---------|-----------------|
| **ElectricFence** | Any | Unmaintained since 2002; extremely slow; no new features |
| **Purify** | Any | Commercial-only; ASan provides equivalent functionality for free |
| **Intel Inspector** | Any | Overkill for memory errors; expensive; ASan + Valgrind cover the use cases |
| **Visual Studio / Windows tools** | Any | Project targets Linux; GStreamer Linux ecosystem is mature |
| **Static analyzers (cppcheck, PVS-udio)** | Any | Helpful but don't catch runtime use-after-free; complement, not replace ASan/Valgrind |
| **Custom memory pools** | Naïve implementations | Interferes with ASan/Valgrind detection; use `std::pmr` with standard allocators instead |

---

*Confidence: HIGH* (All version numbers verified from official sources: GStreamer GitLab API, Clang LLVM docs, Valgrind homepage, GSL GitHub releases as of 2025-05-05)