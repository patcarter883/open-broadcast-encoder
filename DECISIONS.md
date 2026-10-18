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


