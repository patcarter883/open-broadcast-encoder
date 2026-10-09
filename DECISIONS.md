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
