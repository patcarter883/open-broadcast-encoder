// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <exception>

#include "encode/raw_local_port.h"

namespace raw_local
{
auto resolve_port(const std::string& configured) -> std::uint16_t
{
  if (configured.empty()) {
    return k_default_port;
  }
  try {
    const int parsed = std::stoi(configured);
    if (parsed > 0 && parsed <= 65535) {
      return static_cast<std::uint16_t>(parsed);
    }
  } catch (const std::exception&) {
    // Fall through: a device name is not a port.
  }
  return k_default_port;
}
}  // namespace raw_local
