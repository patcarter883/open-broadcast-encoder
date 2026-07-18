// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <filesystem>
#include <optional>

#include "backplane/backplane.h"
#include "lib/lib.h"

// Persists the user-editable Encode / Output / Receiver / Input configuration to
// a JSON file so the operator does not have to re-enter every setting on each
// launch. JSON details (nlohmann) are confined to settings.cpp, mirroring how
// control.cpp keeps httplib/nlohmann out of the rest of the encoder.
namespace settings
{
// Absolute path to the settings file (creating parent directories is the
// caller's job — save() does it). Honours XDG_CONFIG_HOME / APPDATA, falling
// back to ~/.config/open-broadcast-encoder/settings.json.
std::filesystem::path settings_file_path();

// Serialize lib.input_cfg / encode_cfg / output_cfg / receiver_ctl to disk.
// Best-effort: returns false on any I/O error (never throws). The runtime-only
// receiver session_id is intentionally not persisted.
bool save(const library& lib);

// Populate lib's configs from the settings file. Returns true if a file was
// read and parsed; false (leaving the existing defaults untouched) if the file
// is absent or malformed.
bool load(library& lib);

// Hosted-mode session persistence (M2.7). The credentials a backplane
// allocation returns are written to a SEPARATE 0600 file next to the settings
// file, the instant they are received — before the session is used — so a
// crash between allocate and first use cannot silently orphan the allocation.
// On relaunch the encoder loads it and can resume, or offer
// abandon-and-reallocate. Path: <config-dir>/hosted-session.json.
std::filesystem::path hosted_session_path();
bool save_hosted_session(const hosted_session& session);   // 0600, atomic-ish
std::optional<hosted_session> load_hosted_session();
void clear_hosted_session();  // on clean /stop or successful abandon
}  // namespace settings
