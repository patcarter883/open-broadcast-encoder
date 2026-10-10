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


