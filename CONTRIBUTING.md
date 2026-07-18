# Contributing to open-broadcast-encoder

Thanks for your interest in contributing.

## License and inbound policy

This project is licensed **AGPL-3.0-or-later** (see `LICENSE`). Contributions
are accepted under the same license — inbound = outbound. There is no CLA.

Every commit must carry a **Developer Certificate of Origin** sign-off
(<https://developercertificate.org/>):

```
git commit -s
```

which appends a `Signed-off-by: Your Name <you@example.com>` trailer,
certifying you have the right to submit the work under the project license.

> **Note on future policy:** for *substantial* contributions the project may
> in future introduce a contributor license agreement. Any such change will be
> announced in advance and will never apply retroactively to work already
> merged under the DCO.

## Code of Conduct

Please see the [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) document.

## Getting started

Helpful notes for developers can be found in the [`HACKING.md`](HACKING.md)
document.

In addition to the above, if you use the presets file as instructed, then you
should NOT check it into source control, just as the CMake documentation
suggests.

## Practical notes

- Development happens on branch **`v2`** (FLTK UI); `master` is the older
  Tauri iteration and is not the target for new work.
- Keep clang-tidy and the lint/spellcheck CI passing.
- Never commit compiled binaries — CI rejects ELF/PE/Mach-O files.
- Stream keys, tokens and PSKs never enter URLs, logs, or the UI log pane.
