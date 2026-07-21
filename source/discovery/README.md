<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# LAN bridge auto-discovery (prototype)

Lets the encoder find a rist2rist bridge on the local network instead of the
operator typing the router's address. The bridge (`luci-app-rist2rist`)
advertises the DNS-SD service **`_obr-rist._udp`** over mDNS via OpenWrt's
`umdns`; the encoder browses for it here.

## How it works
- `mdns_parse.{h,cpp}` — pure, socket-free mDNS/DNS-SD wire helpers
  (`build_ptr_query`, `parse_response`). Handles name compression, SRV/A/TXT
  records. Unit-testable without the network.
- `discovery.{h,cpp}` — `discover_bridges(timeout)` multicasts a **QU** PTR
  query to `224.0.0.251:5353` and collects unicast responses for `timeout`.
  Each responder yields `{name, host, port}` (host from the A record, or the
  datagram source address as a fallback).
- Wiring:
  - **UI picker** (`ui.cpp`): a **Bridge (auto-discovered)** dropdown +
    **Find bridges on LAN** button in the Output panel, mirroring the NDI
    device picker. The button browses and (re)populates the dropdown; picking
    an entry fills the RIST Address field (and the model). A lone result is
    auto-selected.
  - **Headless fallback** (`transport::setup_rist_sender`): when the RIST
    output host is blank or the literal `auto`, `discover_bridges()` is called
    at Start and the first responder's `host:port` is used; if nothing answers
    it falls back to `127.0.0.1`, so behaviour is unchanged when unused.

## Verify
Router side — with the bridge enabled, `umdns` publishes the service. From any
LAN host:
```
avahi-browse -r _obr-rist._udp        # or: dns-sd -B _obr-rist._udp
```
should list the router with its listen port. Encoder side — leave the output
host blank (or `auto`) and Start; the sender targets the discovered
`host:port`. The parser is covered by a standalone test that feeds synthetic
mDNS responses (incl. a compression pointer) and asserts the extracted bridge.

## Not yet (prototype scope)
- Only IPv4 A records are resolved (SRV port always used).
- The picker browse runs on the UI thread with a short (~1.2 s) timeout; a
  background thread would keep the UI fully responsive during the browse.
- No continuous/background discovery — browse is on demand (button) or at Start.
- QU-unicast only; a multicast-listen fallback would catch responders that
  ignore the QU bit.
- `ui.cpp`/`ui.h` were hand-edited; `ui.fld` (FLUID source) still needs the two
  widgets added so a future FLUID regenerate doesn't drop them.
