# What the encoder puts in its MPEG-TS

The encoder's RIST output is one MPEG-TS multiplex built by `mpegtsmux` with the
settings in `source/encode/ts_mux.h`:

```
mpegtsmux alignment=<1..7> enable-custom-mappings=true name=tsmux
```

`alignment` is TS packets per buffer, i.e. bytes per RIST datagram (n × 188);
the UI value is clamped to 1..7 and defaults to 7 (1316-byte payloads).

Anything that receives this stream has to accept the signalling below. It is
GStreamer's mapping, not a published one, and it is the only reason AV1 works:
`mpegtsmux` refuses `video/x-av1` unless `enable-custom-mappings` is TRUE
("AV1 requires enabling custom mapping which does not have a public
specification yet").

## AV1 (`video/x-av1,stream-format=obu-stream,alignment=frame`)

| Item | Value |
| --- | --- |
| `stream_type` | `0x06` (private PES) |
| PES `stream_id` | `0xBD`, PES header carries PTS/DTS |
| ES descriptor 1 | registration descriptor (`0x05`), `format_identifier` `"AV1G"` |
| ES descriptor 2 | private descriptor (`0x80`), payload = AV1CodecConfigurationRecord (`av1C`), 4 bytes for 8-bit 4:2:0: `81 01 0c 00` = marker+version 1, profile 0, level 1, 4:2:0 |

`tsdemux` keys the pad off the `AV1G` registration descriptor and then uses the
`0x80` `av1C` to refine the caps, so it emits
`video/x-av1,stream-format=obu-stream,alignment=frame` with `profile`,
`bit-depth-luma`, `chroma-format` and `codec_data` filled in from `av1C`.

Consequences for a receiver:

- A `stream_type 0x06` stream **without** a registration descriptor is dropped
  by `tsdemux` — the program is reported as containing no supported streams.
  FFmpeg's `-f mpegts` AV1 output is exactly that: `stream_type 0x06` with no
  ES descriptors, and `tsdemux` rejects it. Parse the AV1 `0x06`/`AV1G` pair
  explicitly, or demux with FFmpeg (which sniffs the OBU stream).
- The PES payload is raw OBU stream in frame alignment, not Annex-B style
  length-prefixed data, so it can be handed to a decoder as is.

## H.264 / H.265

`stream_type 0x1B` / `0x24`, byte-stream with `h264parse`/`h265parse`
`config-interval=1` repeating SPS/PPS in-band.

## AAC

`avenc_aac` → `aacparse` → `stream_type 0x0F`, `audio/mpeg, mpegversion=4,
stream-format=adts`, no ES descriptors (the reference muxers write none either).

## Regression test

`test/source/ts_mux_test.cpp` muxes an AV1 elementary stream with
`ts_mux::element()` (the same settings the pipeline uses), asserts the PMT
carries `stream_type 0x06` with `AV1G` and the `0x80` `av1C`, then demuxes the
file and asserts `tsdemux` produces a `video/x-av1, stream-format=obu-stream,
alignment=frame` pad. A second case pins the negative: the mux element without
`enable-custom-mappings` fails with "mapping is not supported".
