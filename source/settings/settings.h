#pragma once

#include <filesystem>

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
}  // namespace settings
