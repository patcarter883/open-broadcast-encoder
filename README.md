# open-broadcast-encoder

This is the open-broadcast-encoder project.

# Download

Prebuilt binaries are published with every release — no installer, and
GStreamer is inside the download:

| Platform | File |
|---|---|
| Linux x86_64 | `open-broadcast-encoder-linux-x86_64.AppImage` |
| Windows 10/11 64-bit | `open-broadcast-encoder-windows-x64.zip` |

- Latest release: <https://github.com/patcarter883/open-broadcast-encoder/releases/latest>
- Download page, install steps and checksums: <https://stream.huntervalleyit.net.au/download>

Each artefact publishes a `.sha256` beside it — check it before running, as
neither build is code-signed yet. The NDI runtime is the user's own install
(see Licensing); everything else the encoder needs is bundled.

# Building and installing

See the [BUILDING](BUILDING.md) document.

# Contributing

See the [CONTRIBUTING](CONTRIBUTING.md) document.

# Licensing

Licensed under the **GNU Affero General Public License v3.0 or later**
(AGPL-3.0-or-later). See [`LICENSE`](LICENSE).

Third-party components retain their own licenses — see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). The NDI runtime is **not
redistributed** with this application: install the official NDI runtime
yourself; the encoder detects it at runtime and prompts if it is missing.

Contributions are accepted under the same license with a DCO sign-off — see
[`CONTRIBUTING.md`](CONTRIBUTING.md).

# Secrets on disk

The settings file (stream keys, receiver token) is written with `0600`
permissions and secrets are masked in the in-app log panes. OS keychain
integration is a planned later nicety — until then, treat the settings file
path as sensitive. A future hosted-mode device refresh token will be stored in
the same file under the same rules.
