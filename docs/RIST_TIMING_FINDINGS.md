# RIST timing modes & MPEG‑TS timestamping — findings

Investigation notes for the `encoder → rist2rist → receiver` chain. Captures the
double‑hop receiver crash, the librist timing model, MPEG‑TS timestamping
background, and the recommended (coordinated) fixes.

Status: **analysis + recommendation only.** The receiver is owned by another
session — do not edit receiver code based on this doc; hand the recommendation to
that session.

---

## 1. The blocker: receiver SIGABRT under the double hop

Another session confirmed real end‑to‑end video (H.264 1280×720@30 + AAC 48k
stereo) traversing `encoder → rist2rist:5000 → receiver:5001 → RTMP → mediamtx`,
but the receiver aborts within seconds, reproducibly:

```
rist-common.c:692: receiver_enqueue: Assertion `packet_time < next->packet_time' failed.   (exit 134 / SIGABRT)
```

- Never fires on a **direct** `ristsender → receiver` feed (ran for minutes).
- Only with **rist2rist in the middle** (the double RIST hop).

### Root cause (verified in the vendored librist)

`external/rist-cpp/rist/src/rist-common.c`:

- `receiver_calculate_packet_time()` always returns `packet_time = source_time + time_offset`.
- In `receiver_enqueue()` the crash is in a branch gated **only** on
  `peer->config.timing_mode == RIST_TIMING_MODE_ARRIVAL && retry`:

  ```c
  if (RIST_UNLIKELY(peer->config.timing_mode == RIST_TIMING_MODE_ARRIVAL && retry)) {
      // arrival packet time would be incorrect for a retry packet, so instead
      // we interpolate between packets.  this does assume CBR
      ...
      packet_time = previous->packet_time + (time_per_step * steps_since_previous);
      assert(packet_time < next->packet_time);   // <-- aborts here
  }
  ```

For a **retransmitted** packet in **ARRIVAL** mode, librist cannot trust the
packet's arrival time (it arrived late), so it *interpolates* the time between
the neighbouring queued packets **assuming CBR**. The double hop breaks that
assumption: rist2rist buffers (buffer‑min 1000 ms) then drains/forwards in
bursts, and two RIST legs produce more retransmissions, so neighbouring
`packet_time`s are no longer cleanly CBR/monotonic → the interpolation yields
`packet_time >= next->packet_time` → assert → abort.

It is a **debug assert** (the receiver's librist is built with assertions on /
`NDEBUG` off). It is a real librist edge case, exposed by ARRIVAL mode + a relay.

---

## 2. librist timing modes (code + RIST/MistServer docs)

`enum rist_timing_mode` / URL param `timing-mode` (peer.h, and
[MistServer RIST deep‑dive](https://docs.mistserver.org/protocol/deepdive/ristdeepdive/)):

| value | name | meaning |
|---|---|---|
| **0** | **SOURCE** — "RTP Timestamp (**default**)" | release by the packet's RTP/source timestamp; emit at the rate the sender received the media, plus the buffer |
| 1 | ARRIVAL | release by local arrival time; **interpolates retry packets assuming CBR** (the crash path) |
| 2 | RTC | RTP/RTCP + NTP, for multi‑stream NTP sync; receiver drops every packet until an RTCP SR establishes `time_offset` |

`RIST_DEFAULT_TIMING_MODE = RIST_TIMING_MODE_SOURCE` (0). The known‑good reference
(`github.com/patcarter883/ndi-rist-encoder-cpp`, branch `Cmake-Package-Manager`)
sets **no** `timing-mode` → it runs SOURCE.

### How we ended up on ARRIVAL (and why it's wrong here)

History of this stack:
1. Originally `timing-mode=2` (RTC) → receiver dropped **every** packet
   (`rtc_timing_mode && time_offset==0` gate, `rist-common.c:607`; the offset is
   only bootstrapped from an RTCP SR carrying a real NTP clock we never send,
   `ts_ntp=0`). Symptom: no data, no loss/retransmit counters.
2. Switched to `timing-mode=1` (ARRIVAL) → fixed delivery, but introduced the
   ARRIVAL‑retry interpolation assert that crashes under the double hop.
3. **Correct answer: `timing-mode=0` (SOURCE)** — the librist default. It:
   - never enters the ARRIVAL retry‑interpolation branch (no assert),
   - is not the RTC drop gate (`!rtc`; `time_offset` bootstraps from the first
     data packet, `rist-common.c:627`),
   - orders/paces by the **monotonic source (RTP) timestamp**, which is correct
     for a relay because rist2rist forwards the timestamp unchanged (see §3).

---

## 3. Timestamps across the chain (`ts_ntp` / RTP timestamp)

- The encoder calls `rist_sender_data_write` with `ts_ntp = 0`
  (`transport.cpp → RISTNet.cpp:775`). librist then stamps the RTP timestamp from
  its own clock (`rist.c:610`). **This is standard usage** — FFmpeg's
  `libavformat/librist.c` also passes `ts_ntp = 0`. The stamped timestamps are
  monotonic and increasing.
- **rist2rist preserves the timestamp.** Its relay loop is
  `rist_receiver_data_read2()` → `cb_recv()` → `rist_sender_data_write(b)`, and
  `b->ts_ntp` carries the source timestamp through; `rist_sender_data_write` only
  substitutes a fresh clock value when `ts_ntp == 0`. So the *final* receiver
  sees the **encoder's** original monotonic timestamps, not rist2rist's arrival
  time. → SOURCE mode at the final receiver is well‑ordered.
- librist's RTP timestamp is the 90 kHz media clock, conceptually synced to the
  MPEG‑TS **PCR** (RFC 2250). We do not currently feed a real media PTS/PCR into
  `ts_ntp`; not required, but see §6 (optional improvement).

---

## 4. MPEG‑TS framing for RIST (context)

- `mpegtsmux alignment=7` → 7×188 = **1316‑byte** payloads = the canonical
  RTP/MPEG‑TS datagram. Now operator‑tunable via `encode_config.mpegts_alignment`
  (lower it for low‑MTU cellular so RIST datagrams aren't IP‑fragmented).
- librist sends each `rist_sender_data_write` payload as **one** UDP datagram
  (RIST/GRE+RTP framing ≈ 32 B overhead, `RIST_MAX_PACKET_SIZE = 10000`); it does
  not re‑fragment. So mux alignment directly sets on‑wire packet size.
- Decoupling note: in the encode pipeline video+audio share **one** `mpegtsmux`;
  preview uses **separate** sinks. (Relevant to earlier A/V‑coupling debugging,
  not the timing crash.)

---

## 5. Empirically proven (so it's NOT these)

| stage | test | result |
|---|---|---|
| NDI source | gst‑launch video‑only → filesink | ✅ 6.6 MB |
| full encode pipeline | gst‑launch (v+a) → filesink | ✅ 8.0 MB |
| appsink + app pull pattern | C program | ✅ 4669 kbps |
| librist advanced sender→receiver | C program, exact encoder URL | ✅ full‑rate transfer |

The encoder, NDI ingest, appsink pull, and librist send path are all healthy and
full‑rate. The remaining issues are **timing‑mode (this doc)** and the receiver's
RTMP connect‑retry (below).

---

## 6. Recommended changes (must be applied on ALL hops together)

`timing-mode` **must match across every hop** or modes disagree.

1. **Encoder** — `source/transport/transport.cpp`: sender URL `timing-mode=1 → 0`.
2. **rist2rist** — `files/rist2rist.init`: both the receive (`-i`) and output
   (`-o`) URLs `timing-mode=1 → 0`.
3. **Receiver** (other session) — `build_listener_url()`
   (`source/lib/lib.cpp`): listener URL `timing-mode=1 → 0`.
4. **Receiver librist build** (other session) — build the vendored librist
   **release / `NDEBUG`** so a future invariant violation degrades gracefully
   instead of SIGABRT. A production receiver must not abort on a librist
   `assert()`.

Optional hardening (not required for the fix):
5. Feed a real media timestamp into `ts_ntp` (from the GStreamer buffer PTS /
   MPEG‑TS PCR) instead of 0, for tighter end‑to‑end timing and A/V sync. Keep it
   monotonic. Low priority — SOURCE mode with librist's own clock already works.

Secondary, separate issue: **rtmp2sink first‑start failure** — only connects when
media is already flowing at dial‑out; add connect‑retry on the restream side.

---

## 7. Current working‑tree state (transparency)

- `encoder/source/transport/transport.cpp`: I changed `timing-mode` to `0`
  (uncommitted, not yet rebuilt/deployed).
- `receiver/source/lib/lib.cpp`: a `timing-mode 1 → 0` edit (plus comment) was
  applied **before** the "don't touch the receiver" instruction; the revert was
  blocked by the guardrail, so it is **still applied**. The other session should
  reconcile — note it matches the recommended fix above.
- `rist2rist/files/rist2rist.init`: unchanged (still `timing-mode=1`).

## Sources

- [MistServer — Deepdive into using RIST](https://docs.mistserver.org/protocol/deepdive/ristdeepdive/)
- [librist — VideoLAN GitLab](https://code.videolan.org/rist/librist)
- [FFmpeg libavformat/librist.c](https://www.ffmpeg.org/doxygen/7.0/librist_8c_source.html)
- [RFC 2250 — RTP Payload Format for MPEG1/MPEG2 Video](https://www.rfc-editor.org/info/rfc2250/)
- [GStreamer ristsink](https://gstreamer.freedesktop.org/documentation/rist/ristsink.html)
