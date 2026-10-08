# DECISIONS.md — open-broadcast encoder decisions

Code-repo-level choices for the encoder, in the house style: **choice · rationale · rejected alternatives**.
Product/management-plane decisions and the scope of the specs live in
`open-broadcast-product/DECISIONS.md`.

---

## EN-1 — The encoder restates the portal's transcode gop at the real ingest rate (2026-10-09)

**Choice.** After correcting `schema_version` and `source.codec`, `prepare_hosted_body` also rewrites every
output's `transcode.gop` as two seconds at the rate the encoder is actually running —
`(fps_num * 2 + fps_den / 2) / fps_den`, integer arithmetic, exact for 30000/1001. The codec, scale and
bitrate inside that block remain the portal's decision and pass through untouched.

**Rationale.** The gop is not a decision, it is a duration. The portal states it in frames derived from an
assumed 60 fps (`Allocator::TRANSCODE_FPS`) because the allocate request carries only the POP (DT-22), so it
cannot know the ingest rate. Two seconds at 60 frames is four at the 30 fps the camera link now runs — exactly
the ceiling the destination's own guidance says not to exceed. The encoder is the side that can know the rate,
so it is the side that should state the frame count.

**Rejected.** Leaving the portal's value (a 4 s gop at 30 fps, sitting on the stated limit); having the encoder
adjust the transcode *target* to suit the rate (the target is the portal's decision — DT-22); adding a
`source.fps` field to the allocate request (the backplane refuses anything but the POP by design).

**Known limitation.** The correction fires only when the rate is knowable at that instant, and the hosted
allocation is a separate UI action from Start Encode: `/start` is normally POSTed before the pipeline has
negotiated any caps, so `encode::source_fps()` returns false and the portal's value stands. The rate is only
always knowable on the side that sees the arriving stream — the receiver's source parser — so the durable form
of this rule is to express the gop as a duration and convert it there. Tracked as Task C2 in
`~/.hermes/plans/2026-10-09_101758-open-broadcast-portal-destination-editing.md`.
