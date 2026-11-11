// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <string_view>

// The state of the Input pane's NDI options group, derived from the runtime
// probe alone so the mapping is testable without a display. NDI is a
// proprietary runtime the operator installs; when it is missing the group stays
// on screen with its widgets inactive and says where the runtime comes from,
// rather than disappearing without explanation.
struct ndi_options_state
{
  bool selectable = false;
  bool refreshable = false;
  // Empty when NDI is usable, so the line carries nothing the operator does not
  // need.
  std::string_view status;
};

inline auto ndi_options_state_for(bool runtime_available) -> ndi_options_state
{
  if (runtime_available) {
    return ndi_options_state {true, true, {}};
  }
  return ndi_options_state {
      false, false, "NDI runtime not installed - ndi.video"};
}
