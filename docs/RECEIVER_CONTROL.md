# Receiver control (encoder side)

This documents how **open-broadcast-encoder** drives the partner
**open-broadcast-receiver** (`../open-broadcast-receiver`). The receiver is the
ground-up redevelopment of the original `ndi-rist-server`. The authoritative
wire contract and the full design rationale live in the receiver repo:
`open-broadcast-receiver/docs/CONTRACT.md` and `.../DECISIONS.md`.

## What the encoder does

1. **Media path — unchanged.** The encoder still encodes its input
   (NDI/SDP/MPEG-TS/test) and sends it as RIST (caller, `RIST_PROFILE_ADVANCED`)
   to `output_cfg.host:port`. The receiver listens there.
2. **Control plane.** When the backplane allocates a hosted session, the encoder
   POSTs the allocation's start body to the receiver's REST API (`POST /start`)
   describing the restream destinations and how to treat the stream (copy vs
   reencode); ending the session POSTs `/stop`. This is implemented by
   `source/control/control_client` (`httplib::Client` + `nlohmann/json`) and
   invoked from `main.cpp` (`apply_allocation_to_receiver()`) on the allocation
   worker thread, never the FLTK thread.
3. **Telemetry — unchanged path, now actually answered.** The receiver sends the
   encoder's existing 5-byte `wan_telemetry` (link quality + worst-case RTT)
   back over the RIST OOB channel; `rist_oob_cb` already consumes it to drive
   `remote_oob` adaptive bitrate. The original receiver never sent it.

## Where the control values come from

The encoder has no receiver panel and takes no operator-entered control address,
token or destination list. Everything comes from the hosted allocation:

| Value | Source |
|-------|--------|
| `control_url`, `control_token` | the `hosted_session` record the backplane returns and the node persists (`settings::load_hosted_session()`) |
| `start_body_json` | the same allocation record: session id, ingest, source, and one entry per destination |
| destination URLs and stream keys | inside `start_body_json`, one `{type, url, key_or_streamid}` per output |

The encoder adds only what it alone knows to that body: the measured source
codec, geometry/fps and the available link budget. Without a start body the
session is left unconfigured and the transport log says so.

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
- **Per-output disposition is the allocation's choice.** The receiver contract
  supports copy and transcode per output and the start body carries it per
  output; the encoder does not flatten it to one global setting.
- **Provisioning is best-effort.** Start/Stop POST on the allocation worker
  thread; failures are logged to the transport log and do not block encoding.
  The encoder still streams RIST regardless (a receiver that is already
  listening will restream once provisioned).

## Build

Unchanged from before plus a `nlohmann-json` system dependency:

```sh
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build
```
