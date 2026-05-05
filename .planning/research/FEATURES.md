# FEATURES.md — Feature Landscape

## Table Stakes

Features users expect. Without them, the product feels broken or incomplete.

- [x] **Multiple codec support (H.264/H.265/AV1)** — Users need codec flexibility for different use cases: H.264 for compatibility, H.265 for efficiency, AV1 for future-proofing. Missing any codec limits market fit.

- [x] **Hardware encoder support (AMD/NVIDIA/Intel QSV)** — Essential for low CPU usage and professional workflows. Software encoding alone creates CPU bottlenecks that make streaming impossible on many systems.

- [x] **RIST protocol transport** — Professional broadcasters require RIST for reliable low-latency streaming over unpredictable networks. SRT is common, but RIST is specifically requested in this codebase.

- [x] **NDI input support** — NDI is the de facto standard for IP video production. Users expect to ingest NDI sources from cameras, switchers, and other production equipment.

- [x] **SDP/RTP and MPEG-TS input** — Standard broadcast input protocols. Without SDP support, the encoder cannot receive professional ST 2110 streams. Without MPEG-TS, it cannot ingest satellite/terrestrial feeds.

- [x] **Adaptive bitrate control** — Users expect the encoder to automatically adjust bitrate based on network conditions. Manual-only bitrate adjustment causes dropped frames or failed streams during network fluctuations.

- [x] **Real-time statistics display** — Bandwidth, packet loss, RTT, and quality metrics are critical for diagnosing stream health. Users leave if they cannot see what's happening with their stream.

- [x] **Basic GUI controls** — Start/stop, input selection, encoder selection, bitrate configuration. Without these, the product is unusable for non-developers.

## Differentiators

Features that set the product apart from competitors. **v1 recommendation in parentheses.**

- [x] **Correct GStreamer element mapping (v1, LOW complexity)** — The current codebase has critical bugs: `h264parse` used for H.265 output, `x264enc` used for NVENC AV1 instead of `nvav1enc`. Fixing these is a must-have bug fix, not a feature, but getting it right distinguishes from incomplete implementations.

- [x] **Memory safety with RAII wrappers (v1, MED complexity)** — Use-after-free in `pull_video_buffer` (returns pointer to unmapped buffer data) and unbounded stats vectors cause crashes. Proper `gsl::owner` wrappers and bounded circular buffers provide reliability that hobbyist tools lack.

- [x] **Real-time NDI device discovery (v2, HIGH complexity)** — Automatically detecting NDI sources on the network without manual refresh. OBS requires manual refresh; professional tools auto-discover.

- [x] **Multi-stream RIST bonding (v2, HIGH complexity)** — Simultaneously stream to multiple destinations with automatic failover. Competes with Haivision encoders. Requires `streams` config already exists but isn't fully implemented.

- [x] **Hardware-accelerated codec detection (v2, LOW complexity)** — Probe available GStreamer plugins at startup and only show working encoder options. Prevents user frustration from selecting non-functional combinations.

- [x] **Profile/level selection for encoders (v2, MED complexity)** — Let users select H.264 baseline/main/high profile for compatibility with specific decoders. Required for broadcast compliance.

- [x] **Buffer underrun recovery (v2, MED complexity)** — Detect and recover from temporary network outages without dropping the stream entirely. Professional differentiator vs basic streaming tools.

## Anti-Features

Things to deliberately NOT build in v1. These add complexity without sufficient value.

- [ ] **Custom filter graph editor** — Complex UI feature for chaining arbitrary filters. OBS has this; building it duplicates effort. Focus on reliable encoding first.

- [ ] **Recording to file** — The codebase is an encoder, not a recorder. Adding file output conflates responsibilities. Users who need recording should use OBS or ffmpeg separately.

- [ ] **Browser source/dockable panels** — WebRTC browser sources and complex UI docking. These are OBS's focus; this encoder targets reliable professional streaming.

- [ ] **Scene collections/multi-scene switching** — Multiple scenes with transitions. This is a video mixer feature; encoder should focus on single-stream reliability.

- [ ] **Chat/alerts integration** — Twitch/YouTube chat overlays and event notifications. These are content creator features, not broadcast reliability features.

- [ ] **Plugin architecture** — Dynamic loading of custom modules. Adds complexity and potential instability. Static linking with clear configuration is better for v1.

- [ ] **Audio mixing board** — Multi-channel audio mixing with VST plugin support. Use the system mixer or dedicated audio software instead.

## Feature Dependencies

Feature B requires Feature A:

- `Multi-stream RIST bonding` → `Single-stream RIST` (already partially implemented)
- `Hardware-accelerated codec detection` → `Hardware encoder support` (partially implemented)
- `Profile/level selection` → `Correct GStreamer element mapping` (must fix element bugs first)
- `Buffer underrun recovery` → `Adaptive bitrate control` (ABR provides the foundation)
- `Real-time NDI discovery` → `NDI input support` (basic NDI needs to work first)

## Sources

- [OBS Studio Wiki](https://obsproject.com/wiki/) — Market leader for streaming software. Shows users expect multiple input sources, hardware encoder support, adaptive streaming, and real-time stats.
- [FFmpeg Documentation](https://ffmpeg.org/documentation.html) — Reference for correct encoder element names (`h264parse` vs `h265parse`, `nvav1enc` vs `x264enc`).
- [GStreamer Plugins Reference](https://gstreamer.freedesktop.org/documentation/plugins.html) — Shows available elements for each codec/hardware combination.
- [Haivision Makito X4](https://www.haivision.com/products/makito-x4/) — Professional RIST encoder. Shows multi-stream bonding and reliability features.
- [Wirecast](https://www.telestream.net/wirecast/overview.htm) — Professional streaming solution. Shows scene-based production vs single-encoder focus.

---

*Confidence: HIGH (OBS documentation + GStreamer plugin docs + codebase analysis verified)*