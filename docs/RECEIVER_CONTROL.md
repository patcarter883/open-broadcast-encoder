# Receiver control (encoder side)

This documents the changes that let **open-broadcast-encoder** drive the partner
**open-broadcast-receiver** (`../open-broadcast-receiver`). The receiver is the
ground-up redevelopment of the original `ndi-rist-server`. The authoritative
wire contract and the full design rationale live in the receiver repo:
`open-broadcast-receiver/docs/CONTRACT.md` and `.../DECISIONS.md`.

## What the encoder now does

1. **Media path — unchanged.** The encoder still encodes its input
   (NDI/SDP/MPEG-TS/test) and sends it as RIST (caller, `RIST_PROFILE_ADVANCED`)
   to `output_cfg.host:port`. The receiver listens there.
2. **Control plane — new.** On **Start**, the encoder POSTs a configuration to
   the receiver's REST API (`POST /start`) describing one or more restream
   destinations and how to treat the stream (copy vs reencode). On **Stop** it
   POSTs `/stop`. This is implemented by `source/control/control_client`
   (`httplib::Client` + `nlohmann/json`), invoked from `main.cpp`
   (`provision_receiver()` / `release_receiver()`) on detached threads so the
   FLTK UI never blocks on receiver network I/O.
3. **Telemetry — unchanged path, now actually answered.** The receiver sends the
   encoder's existing 5-byte `wan_telemetry` (link quality + worst-case RTT)
   back over the RIST OOB channel; `rist_oob_cb` already consumes it to drive
   `remote_oob` adaptive bitrate. The original receiver never sent it.

## The new UI section ("Receiver / Restream")

A full-width strip below the Input/Encode/Output/Stats row:

| Control | Writes to | Notes |
|---------|-----------|-------|
| **Enable receiver** | `receiver_ctl.enabled` | when off, Start/Stop do not contact a receiver |
| **Receiver host:port** | `control_host` / `control_port` | the receiver's HTTP control endpoint (default `127.0.0.1:8080`) — distinct from the RIST media address |
| **Token** | `token` | Bearer token; must match the receiver's `--token` |
| **Reencode** | `reencode` | off ⇒ copy/passthrough; on ⇒ transcode (applies to all destinations in v1) |
| **Codec / Encoder / Bitrate kbps / Upscale 1440p** | `video.*` | reencode settings (the receiver may use different hardware than the encoder) |
| **Destinations** (multiline) | `destinations` | one per line: `rtmp\|rtmps\|srt\|rist  <url>  [key]` |

Example destinations:

```
rtmp  rtmp://a.rtmp.youtube.com/live2  abcd-efgh-ijkl
srt   srt://203.0.113.9:9000
rist  rist://203.0.113.9:7000
```

## Key decisions (encoder side)

- **REST/JSON control, not the old rpclib.** Rationale in the receiver's
  `DECISIONS.md §1`. Header-only client (`external/httplib.h`, pinned), system
  `nlohmann-json`; no vcpkg, no new linked libraries.
- **`codec`/`encoder` enum integer order is the shared wire contract.** The
  receiver maps the same JSON strings to the same enum values; `lib.h` must not
  renumber them. New `output_proto` enum added for destination types.
- **Copy mode sends `video.codec == source.codec`.** The control client sets the
  source codec from `encode_cfg.selected_codec`; in copy mode the destination
  codec mirrors it (the receiver rejects a mismatch).
- **RTMP carries H.264 only.** If you target RTMP with H.265/AV1 the receiver
  returns `400 rtmp_codec_unsupported`; the encoder logs it to the transport log.
- **v1 simplification:** reencode disposition is global (applies to every
  destination) in the UI, even though the contract supports per-output
  disposition. Per-destination reencode is a straightforward follow-up.
- **Provisioning is best-effort and detached.** Start/Stop POST on a detached
  thread; failures are logged to the transport log and do not block encoding.
  The encoder still streams RIST regardless (a receiver that is already
  listening will restream once provisioned).

## Build

Unchanged from before plus a `nlohmann-json` system dependency:

```sh
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build
```
