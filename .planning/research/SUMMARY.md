# Research Synthesis: Open Broadcast Encoder

**Domain**: C++20 video streaming encoder with GStreamer pipelines, FLTK GUI, RIST transport

**Compiled**: 2026-05-05 from 4 specialist research files

---

## Key Findings

1. **Critical Memory Safety Issues Require Immediate Fix** — Three use-after-free/dangling pointer vulnerabilities exist: (1) `pull_video_buffer()` returns pointer to GStreamer buffer after unref, (2) global `encoder_ptr` can dangle during RIST callbacks, (3) unbounded stats vectors cause memory exhaustion. AddressSanitizer + Valgrind verification essential before any feature work.

2. **GStreamer Codec Bugs Produce Invalid Output** — Wrong parser elements in use: `h264parse` for H.265 output and `x264enc` for NVENC AV1 instead of `nvav1enc`. These fundamental bugs prevent correct stream generation and waste hardware encoding capabilities.

3. **Architecture is Layered Modular Monolith** — Six components (lib→encode→transport→ui→ndi_input→stats) communicate via global state and callbacks. Build order is strict: Core Types first, then encode/transport depend on it, UI and stats depend on both.

4. **FLTK Threading Model is Non-Negotiable** — All UI updates from background threads (stats callbacks, NDI discovery) require `Fl::lock()/unlock()` wrapping. RIST statistics callback currently violates this, causing intermittent crashes.

5. **v1 Feature Set is Well-Defined & Testable** — Table stakes features (codecs, hardware encoders, RIST, NDI, SDP/MPEG-TS, adaptive bitrate, stats, GUI) form a complete minimum viable product. Differentiation comes from fixing correctness bugs competitors haven't.

6. **Memory Tools Stack Provides Comprehensive Coverage** — AddressSanitizer for fast iteration, Valgrind for thoroughness, GDB for interactive debugging, and GSL `owner<T>` for compile-time lifetime safety. Static analyzers alone insufficient for use-after-free detection.

7. **Architecture has Hard-to-Reverse Decisions** — Global state pattern, callback-based communication, and raw pointer encoder reference are foundational choices that touching any component requires careful coordination.

---

## Critical Gotchas

| Pitfall | Symptom | Root Cause | Detection |
|---------|---------|------------|-----------|
| **Buffer use-after-free** | Random crashes in transport, corrupted video | `info.data` returned after `gst_sample_unref()` | ASAN_OPTIONS=quarantine_size_mb=512 |
| **Wrong codec parsers** | Invalid streams, high CPU (no HW encode) | `h264parse` used for H.265, `x264enc` for AV1 | `gst-inspect-1.0` element verification |
| **Dangling encoder pointer** | Crashes on stop, segfault in `set_encode_bitrate()` | RIST callback races with encoder destruction | GDB breakpoint on `rist_stats_cb` |
| **Stats vector growth** | Memory exhaustion after 19+ hours | Unbounded `push_back` in 14Hz callback | Monitor process RSS over time |
| **FLTK threading violation** | UI corruption, X11 errors | UI updates without `Fl::lock()` in RIST callback | Helgrind race detection |

**Verification Triggers**: Run with `GST_DEBUG=refcounting:5,error:5` and `ASAN_OPTIONS=detect_stack_use_after_return=true:quarantine_size_mb=512` to expose latent memory bugs.

---

## Implications for Roadmap

### Phase 1: Stability Foundation (MUST DO FIRST)

**Gate Criteria**: No memory errors under ASan/Valgrind, correct codec streams verified with `gst-launch-1.0`, UI stable under thread-sanitizer.

1. **Memory Safety Hardening**
   - Replace `pull_video_buffer()` dangling pointer with copied buffer (`gst_buffer_extract_dup()`)
   - Convert raw `encoder_ptr` to `weak_ptr` pattern with `lock()` check
   - Replace unbounded stats vectors with `boost::circular_buffer` or fixed-size ring

2. **GStreamer Correctness**
   - Fix H.265 pipeline to use `h265parse` (not `h264parse`)
   - Fix NVENC AV1 to use `nvav1enc` (not `x264enc`)
   - Add codec-to-parser mapping verification at pipeline build time

3. **Threading Compliance**
   - Wrap all RIST callback UI updates in `ui.lock()/unlock()`
   - Verify with Helgrind: `valgrind --tool=helgrind ./open-broadcast-encoder`

### Phase 2: v1 Feature Completion

**Dependent on**: Phase 1 stability gate passed.

1. **Table Stakes Features** (build on stabilized foundation):
   - Multiple codec support (H.264/H.265/AV1) with correct parsers
   - Hardware encoder support (AMD/NVIDIA/Intel QSV)
   - RIST protocol transport (single stream)
   - NDI input support (basic discovery)
   - SDP/RTP and MPEG-TS input
   - Adaptive bitrate control
   - Real-time statistics display
   - Basic GUI controls

### Phase 3: v2 Differentiation

**Dependent on**: v1 features working reliably.

1. **Real-time NDI device discovery** → requires stable NDI foundation
2. **Multi-stream RIST bonding** → requires `streams` config activation
3. **Hardware-accelerated codec detection** → probe GStreamer plugins at startup
4. **Profile/level selection** → requires correct element mapping first
5. **Buffer underrun recovery** → requires ABR stability

### What NOT to Build (v1 Anti-Features)

- Custom filter graph editor — duplicates OBS effort
- Recording to file — beyond encoder scope
- Browser source/dockable panels — content creator features
- Scene collections — video mixer territory
- Chat/alerts integration — not broadcast reliability

---

## Cross-Reference Map

| Component | Memory Risk | Threading Risk | Codec Risk | Feature Status |
|-----------|-------------|----------------|------------|----------------|
| `lib` | ✅ Low | ✅ Low | ✅ None | ✅ Stable |
| `encode` | ⚠️ High (buffer) | ⚠️ Med (bus thread) | 🔴 Critical (wrong parsers) | ⚠️ Needs fix |
| `transport` | ⚠️ Med (global ptr) | ⚠️ High (RIST callback) | ✅ Low | ⚠️ Needs fix |
| `ui` | ✅ Low | ⚠️ Med (FLTK locks) | ✅ None | ✅ Working |
| `ndi_input` | ✅ Low | ⚠️ Med (device monitor) | ✅ Low | ✅ Working |
| `stats` | 🔴 Critical (unbounded vec) | 🔴 Critical (FLTK violation) | ✅ Low | ⚠️ Needs fix |

---

*Confidence: HIGH — All findings cross-verified between STACK, FEATURES, ARCHITECTURE, and PITFALLS research files.*