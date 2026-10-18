# DECISIONS.md — open-broadcast encoder decisions

Code-repo-level choices for the encoder, in the house style: **choice · rationale · rejected alternatives**.
Product/management-plane decisions and the scope of the specs live in
`open-broadcast-product/DECISIONS.md`.

---

## EN-1 — The encoder restates the portal's transcode gop at the real ingest rate (2026-10-09)

**Choice.** After correcting `schema_version` and `source.codec`, `prepare_hosted_body` also rewrites every
output's `transcode.gop` as two seconds at the rate the encoder is actually running — `ingest_fps * 2`, where
`ingest_fps` is the measured rate **snapped to a standard broadcast rate** (24/25/30/50/60, nearest) by
`fps_snap::standard_rate` and cached when first measured. The codec, scale and bitrate inside that block remain
the portal's decision and pass through untouched.

The rate is measured where a pipeline is live: `encode::source_fps_snapped()` reads the video encoder's own
sink-pad caps, and caches the snapped result because Allocate and Start Encode are independent UI actions.
**Snapping is the rule, not rounding** — a source measuring 29.97 or 30.17 must give 30 (a gop of 60, not 59 or
61), and a stalled source reading 29.0 must not give 58. No input configuration or setting carries a rate, which
is why the measurement comes from caps rather than from the input step's settings.

**Rationale.** The gop is not a decision, it is a duration. The portal states it in frames derived from an
assumed 60 fps (`Allocator::TRANSCODE_FPS`) because the allocate request carries only the POP (DT-22), so it
cannot know the ingest rate. Two seconds at 60 frames is four at the 30 fps the camera link now runs — exactly
the ceiling the destination's own guidance says not to exceed. The encoder is the side that can know the rate,
so it is the side that should state the frame count.

**Rejected.** Leaving the portal's value (a 4 s gop at 30 fps, sitting on the stated limit); having the encoder
adjust the transcode *target* to suit the rate (the target is the portal's decision — DT-22); adding a
`source.fps` field to the allocate request (the backplane refuses anything but the POP by design).

**Known limitation.** The measurement can only be taken once a pipeline has negotiated caps, so an allocation
POSTed *before* Start Encode sees no rate and the portal's value stands — the two UI actions are independent and
the cache cannot be filled any earlier. Correcting that ordering would need the allocate path to wait for the
first measurement, or to re-post once the rate is known; neither is in scope here.

**Withdrawn.** An earlier form of this entry proposed moving the rule to the receiver ("the rate is only always
knowable on the side that sees the arriving stream"). That was wrong: the encoder can measure the rate before it
assigns or starts, so the encoder is the single correct home for this rule and the receiver's contract stays
untouched. The plan tracked that as Task C2, now collapsed.

---

## EN-2 — The calibration leaves the bridge SHAPED: SQM is started unconditionally (2026-10-10)

**Choice.** `method_calibrate` now ends EVERY path through one function, `shaper_finalise`. If the run cleared a
shaper, it restores exactly that (`shape_resume`, which starts SQM and verifies each leg). Otherwise, if the
box's OWN SQM config names a queue for one of the present legs, it runs `sqm start` **unconditionally** — SQM
comes up even when the run derived no rate at all — and then verifies the legs are actually shaped rather than
assuming it. The outcome is reported as two new `shaper` values: `started` (SQM was down and came up on the
config's rates) and `start_failed` (it did not). `applied` still wins when this run derived and applied its own
rate. A bridge with **no configured queue** is left untouched and still reports `none`.

**Rationale.** `start` was gated on the run having derived a rate, and the shaper was only ever resumed if the
run had cleared one — so on a box whose SQM anything had stopped (another agent, a previous run, a reboot), the
derived rate was written to uci, nothing was started, and the operator was handed an **unshaped WAN while the
report said `none`**. That is not cosmetic: an unshaped leg fills the **modem's own queue**, so the NEXT
calibration measures the modem instead of the link, and the shaper the operator asked for never exists. Seen
live: SQM inactive, `wwan0` carrying only the kernel's default `fq_codel`, while `uci` said `qdisc=cake
upload=675`. `sqm start` is idempotent, so unconditional is safe.

**Rejected.** Gating the start on "was anything cleared" (the state that produced the defect); starting SQM in
two places (the derived-shaper path and the resume path — the two drifted apart, which is how the defect
survived a fix); reporting `applied` for a config-rate start-up, which would claim a measurement this run never
made; starting SQM on a bridge whose config names no queue, which would install a shaper the operator never
configured.

**Ingress (downstream) shaping — IMPLEMENTED, option (a) chosen (2026-10-10).** `shape_configure` set only
`upload`; each leg's `download` stayed a static `10000` that was never derived — exactly the "wrong shaper"
class this feature exists to remove. Pat chose the measured route over a policy constant, so a new instrument,
`rist2rist-downlink`, measures ONE leg's ingress with a leg-bound HTTP fetch (`wget --bind-address`) and meters
the **leg's own `rx_bytes`**.

Four calls that matter, each rejecting a cheaper wrong answer:

- **Meter the leg, not the fetch.** A bound fetch on a leg with no usable route still completes and would
  report a perfect rate for a link it never touched. The leg's own counter is the only thing that proves which
  leg carried it, so the tool refuses (`no_leg_traffic`, exit 1) when the leg moved less than `MIN_LEG_BYTES`.
- **HTTP, not a ramp.** The uplink probe must ramp because UDP never back-pressures; TCP does, so congestion
  control finds the achievable rate and a ramp would only add bytes.
- **Bounded twice.** A server-side byte target AND a wall-clock cap: a metered link must not be drained, and a
  slow leg must not hold the run open. A fetch killed by the clock is a measurement of a slow leg, not a failure.
- **A monotonic clock** (`/proc/uptime`), not `date +%s`: a 1 s clock over a ~10 s window is a 10 % error on the
  very figure being produced (busybox `date` has no `%N`).

The ingress figure is written only when a downlink measurement EXISTS — no guess — and is kept OUT of the egress
arithmetic that sizes `bandwidth=`. Report gains `download_kbps` / `download_shaper_kbps` per leg.

**Trap hit while wiring it:** `sv=$(shape_configure …)` is a command substitution, i.e. a **subshell**, so an
assignment made inside the function is discarded — the uplink value was already collected by the *caller* for
exactly this reason, and the downlink value is now echoed on a second line for the caller to collect too. A
report field that silently stays null is what this looks like from outside.

---

## EN-3 — The encode pipeline runs on the LAN's PTP clock, so its PTS is an absolute instant (2026-10-19)

**Choice.** The encode pipeline takes its clock from the video LAN's PTP grandmaster. In `encode::parse_pipeline`,
once the pipeline has parsed and while it is still in NULL, it is put on the process's shared clock — the PTP
clock when a grandmaster answers (`gst_ptp_clock_new`) — with `gst_element_set_start_time(pipeline,
GST_CLOCK_TIME_NONE)` and `gst_element_set_base_time(pipeline, 0)`. The clock is acquired **once per process** and
cached, so every later Start runs on the same timeline; `OBC_PTP=off` forces the fallback. Without a master the
monotonic system clock is used and the log says so **loudly**, naming what is lost, rather than emitting
quietly-misaligned timestamps. GStreamer's `gst-ptp-helper` must carry `cap_net_bind_service` **and**
`cap_net_admin` — a `setcap` REPLACES the set, it does not add to it.

**Base time 0, chosen deliberately.** A PTS of 0 at base time 0 *is* the instant the clock reports, so a live
source's timestamped frame reaches the wire naming the moment on the LAN timeline instead of "time since this
pipeline started". The recorder's recipe (`gst_clock_get_time(clock) + margin`) is right for the recorder and
wrong here: a take's timeline begins when the operator pressed record, whereas this encoder **emits** and must name
the instant a frame happened. The router serves **UTC as PTP time on purpose** — every consumer on this side works
in `CLOCK_REALTIME`, so a 37 s TAI offset would misalign them; nothing here "corrects" it to TAI.

**Rationale.** Camera nodes, this encoder, the recorder and OBS are to land on one timeline. The camera link
already carries PTP-referenced MPEG-TS; the encoder was the one hop that re-stamped everything relative to its own
pipeline start, which makes two runs — or two machines — incomparable by construction. Where the pipeline clock
comes from is the one place that decision belongs.

**Rejected.** `gst_system_clock_obtain()` alone (the previous behaviour: right within one process, meaningless
across machines); a base time read off the clock at Start (turns an absolute PTS back into a session-relative one
— the recorder's need, not this one); `GST_CLOCK_TIME_NONE` start time *without* base time 0 (the pipeline then
recomputes the base at PLAYING and throws the absolute value away).

**Needs `gstreamer-net-1.0`.** `GstPtpClock` lives in `gst/net`, i.e. `libgstnet-1.0`, which the plain
`gstreamer-1.0` pkg-config module does not cover — hence the added `pkg_search_module` and link entry. The header
must be `#include <gst/net/gstptpclock.h>`: since GStreamer 1.2x `<gst/net/gstnet.h>` no longer pulls in the PTP
header, and with the umbrella header `gst_ptp_is_supported()` reads as undeclared while `gst_ntp_clock_new()`
still resolves.

**Scope.** A direct request, outside the v1 bug-fix phase plan (`SPEC.md` / `ROADMAP.md` cover memory safety and
codec correctness only), so it is recorded here rather than folded into a phase. The preview pipelines
(`main.cpp`'s general and OBS-raw previews, `ndi_input`'s preview) are deliberately **left on the default clock**:
they display, they emit nothing to the LAN, and a monitor that showed live frames shifted onto a remote timeline
would be a worse monitor, not a better one. Only the encode pipeline feeds the network.

**Verified / not verified (2026-10-19).** Verified by execution: it builds clean; and with no master reachable
(`gst_ptp_is_supported()` TRUE, `gst_ptp_init()` succeeding, `gst_clock_wait_for_sync` timing out) the real
`encode::run_encode_thread()` path logs the fallback line and the pipeline still parses, plays and stops. The
base-time recipe was measured on a live pipeline: PTS ≈ 0.002 s under the default, PTS = the clock's own reading
within 53 ms with this recipe. **Not verified:** a PTP-locked run — the grandmaster (192.168.1.1) sits on the
video LAN segment and PTP multicast does not route to this box, so the lock path is reasoned-and-reviewed only.

---

## EN-4 — The no-grandmaster fallback is CLOCK_REALTIME, not MONOTONIC (2026-10-19)

**Choice.** When `GstPtpClock` does not lock, `sync_clock()` falls back to the system clock with
`clock-type = GST_CLOCK_TYPE_REALTIME` (previously `GST_CLOCK_TYPE_MONOTONIC`). Every path now logs which
clock was chosen: the PTP branch says so, each of the three no-lock branches names `CLOCK_REALTIME`, and the
fallback logs that it is comparable across machines **only if this host's clock is PTP-disciplined**
(`timedatectl` / a running `ptp4l`). `OBC_PTP=off` logs its own line instead of silently skipping the PTP probe.

**Why REALTIME and not MONOTONIC.** On a host whose `CLOCK_REALTIME` is PTP-disciplined, REALTIME *already is*
the LAN timeline, so an emitted PTS stays comparable with the recorder, the camera nodes and OBS — the whole
point of EN-3 — even on the fallback path. MONOTONIC is time-since-boot: a *different* timeline, whose PTS look
perfectly healthy while silently destroying cross-machine alignment. Picking a timeline that cannot align
anything, and doing it silently, is the defect EN-3 was meant to remove; the fallback was the half it missed.

**The fallback is the operative path on this rig, by measurement.** `gst-ptp-helper` (which implements
`GstPtpClock`) binds UDP 319/320; so does `ptp4l`; there is no `SO_REUSEPORT`, so only one binder wins. Where
`ptp4l` runs — every camera node, the OBS box — `GstPtpClock` can never lock, and the code takes the REALTIME
fallback. Confirmed on the Pi (192.168.1.118, `ptp4l-slave` slaved to the router): with `ptp4l` running,
`sudo ss -lunp` shows `ptp4l` holding `0.0.0.0:319`/`:320` and a `gst_clock_wait_for_sync` returns FALSE for a
20 s wait; with `ptp4l-slave` stopped and 319/320 free, the helper still did not lock within 20 s (it *selected*
grandmaster `0x60beb4fffe24f277` from the announces but logged `Currently selected clock timed out, downgrading`
and never reached synced). `ptp4l-slave` was restarted immediately and re-verified (`gmPresent true`,
`master_offset ≈ −46 µs`). So on this LAN the REALTIME fallback is not a corner case — it is what every node
runs, which is exactly why its timeline must be the right one.

**Rejected.** `GST_CLOCK_TYPE_MONOTONIC` (incomparable to anything, and silently so); `gst_system_clock_obtain()`
without setting the type (defaults to MONOTONIC — the pre-EN-3 behaviour); an NTP clock (`gst_net_client_clock`)
(a second time source with its own offset and no PTP traceability); refusing to run without a lock (the fallback
is legitimate and comparable here).

**Verified / not verified (2026-10-19).** Verified by execution, on the real `encode::run_encode_thread()` path
(a scratch harness drives the pipeline with a `videotestsrc` source): with no lock reachable the log reads
`Clock: no PTP master via GstPtpClock (or gst-ptp-helper cannot bind 319/320 - e.g. ptp4l already holds them) -
falling back to CLOCK_REALTIME.` followed by `Clock: using CLOCK_REALTIME - comparable across machines ONLY if
this host's clock is PTP-disciplined (timedatectl / ptp4l).`; with `OBC_PTP=off` the log reads `Clock:
OBC_PTP=off - using CLOCK_REALTIME.` then the same second line. The build is clean (`cmake --build build`, RC 0,
`encode.cpp` recompiled and relinked). **Not verified:** a PTP-*locked* run — the grandmaster (192.168.1.1) is
not reachable from this box, and even with 319/320 freed on the Pi the lock did not complete (above), so the
locked branch is reasoned-and-reviewed only.

---

## EN-5 — The lock fails because the grandmaster answers only unicast DELAY_REQ; `gst_ptp_init(0, NULL)` is not the cause (2026-10-19)

**Choice.** No code change. A source-level and on-wire investigation of "`gst_clock_wait_for_sync()` returns FALSE
with the master plainly announcing" finds that (a) `gst_ptp_init(0, NULL)` is **not** an empty interface list and
never was, and (b) the sync cannot complete because this LAN's grandmaster replies only to a **unicast** (hybrid)
DELAY_REQ while `gst-ptp-helper` always sends its DELAY_REQ to the E2E **multicast** address. Neither is something
this repo can change, so `sync_clock()` is left exactly as EN-3/EN-4 wrote it and the REALTIME fallback remains
the operative path — for a deeper reason than EN-4 had.

**`NULL` means "all compatible interfaces", not "none".** `gst_ptp_init_full()` only appends `-i <iface>` for
each element when `interfaces != NULL`; with `NULL` it appends none and the helper falls back to its own
"every compatible interface" default. Measured on the OBS box (192.168.1.235), ports 319/320 free, same process
shape as this encoder: `gst_ptp_init(0, NULL)` and `gst_ptp_init(0, ["enp7s0"])` behave **identically** — both log
`Binding to interface enp7s0`, both elect master `0x60beb4fffe24f277`, both then fail to sync. The
`interfaces: []` line that suggested an empty list is the **GstStructure config dump** at `GST_DEBUG=ptp:6`, not
the helper's interface set. Passing an explicit list is therefore not a fix, and is not a safe substitute either:
the helper calls `bail!` (`"Not all selected network interfaces found"`) if any named interface is not one it
considers compatible, so an explicit list can make `gst_ptp_init()` **fail** where `NULL` succeeded (e.g. a box
with a `tun`/`wg` interface UP).

**What actually blocks the lock.** With the helper bound and the master elected, the exchange runs as
SYNC → FOLLOW_UP → DELAY_REQ → (no DELAY_RESP). `gstptpclock.c` is compiled with `USE_ONLY_SYNC_WITH_DELAY`, so
without the DELAY_RESP it never reaches `update_ptp_time()`, `domain->last_ptp_time` stays 0, the GstPtpClock
never reports `synced`, and every second the PTP thread logs `Currently selected clock timed out, downgrading`.
`tcpdump` on enp7s0 over a 45 s probe: 359 SYNC, 359 FOLLOW_UP, 23 ANNOUNCE from 192.168.1.1, our DELAY_REQ
(`0x9c6b00fffe0749fa` → `224.0.1.129:319`) — and **zero** DELAY_RESP. A/B against the same master with `ptp4l`:
`hybrid_e2e 1` (DELAY_REQ sent unicast to 192.168.1.1) draws **8 DELAY_RESP**; plain E2E (DELAY_REQ sent to
`224.0.1.129`) draws **0**. The grandmaster is therefore hybrid-only, which is exactly what the old
`/etc/ptp4l-slave.conf` (`hybrid_e2e 1`) was written for. GStreamer has no equivalent option: the Rust helper's
send path is `send_to(buf, (PTP_MULTICAST_ADDR, PTP_EVENT_PORT))` for every event message.

**Consequence for EN-3/EN-4.** They stand as written — the PTP clock is still the clock the encoder *wants*, and
the REALTIME fallback is still the one it *runs* — but EN-4's account of the fallback ("`ptp4l` holds 319/320, so
the helper cannot bind") is only half of it. Freeing the ports is necessary and **not sufficient**: even with the
helper bound and the master elected, this rig's grandmaster will not answer a multicast DELAY_REQ. The two real
fixes are outside this repo: allow multicast E2E delay-req on the router (it must not be changed — the camera
link and the Pi's `ptp4l` depend on it), or make `gst-ptp-helper` send DELAY_REQ unicast (an upstream change).

**Rejected.** Switching `sync_clock()` to `gst_ptp_init(0, [...])` (behaviour-identical here, and a regression
risk elsewhere — above); disabling the PTP probe and going straight to REALTIME (loses the lock on any conformant
master, which is the whole of EN-3); tightening the wait or retrying (the DELAY_RESP never arrives, so no wait
length helps).

**Verified / not verified (2026-10-19).** Verified by execution on the OBS box, `ptp4l-slave` stopped so the
helper owns 319/320: `GST_DEBUG=ptp:6` shows `Binding to interface enp7s0` and `Selected new master clock`,
`wait_for_sync` returns FALSE after 45 s, and the wire capture shows no DELAY_RESP (vs 8 with `ptp4l hybrid_e2e`).
**Not verified:** any configuration in which `wait_for_sync` returns TRUE on this rig — none was found. The
encoder binary itself was not run headless (Start Encode needs an input source and a live pipeline); the finding
is from the same GStreamer PTP code path driven directly, and the builds are clean (`cmake --build build`, RC 0).



