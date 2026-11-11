// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The Input pane's NDI options are gated on a runtime probe, not a build flag.
// These pin the probe's answer against the GStreamer registry and the widget
// state that answer produces, so neither half can drift on its own.

#include <string>
#include <string_view>

#include "ndi_input/ndi_runtime.h"

#include <catch2/catch_test_macros.hpp>
#include <gst/gst.h>

#include "lib/ndi_options.h"

namespace
{
auto registry_has_ndi_element() -> bool
{
  for (const char* name : {"ndisrc", "ndisink"}) {
    GstElementFactory* factory = gst_element_factory_find(name);
    if (factory != nullptr) {
      gst_object_unref(factory);
      return true;
    }
  }
  return false;
}
}  // namespace

TEST_CASE("the probe answers exactly as the registry does", "[ndi]")
{
  gst_init(nullptr, nullptr);

  // Both directions matter: a missing runtime must not read as available (the
  // picker would look usable and find nothing), and an installed runtime must
  // not be hidden.
  REQUIRE(ndi_runtime_available() == registry_has_ndi_element());
}

TEST_CASE("a present runtime leaves the options group usable", "[ndi]")
{
  const ndi_options_state state = ndi_options_state_for(true);

  REQUIRE(state.selectable);
  REQUIRE(state.refreshable);
  REQUIRE(state.status.empty());
}

TEST_CASE("a missing runtime disables both widgets and says why", "[ndi]")
{
  const ndi_options_state state = ndi_options_state_for(false);

  REQUIRE_FALSE(state.selectable);
  REQUIRE_FALSE(state.refreshable);
  REQUIRE(state.status.find("NDI runtime") != std::string_view::npos);
  REQUIRE(state.status.find("ndi.video") != std::string_view::npos);
}

TEST_CASE("the probed result is what the options group is built from", "[ndi]")
{
  gst_init(nullptr, nullptr);

  // The UI's own wiring: probe -> state -> widget activation.
  const bool available = ndi_runtime_available();
  const ndi_options_state state = ndi_options_state_for(available);

  REQUIRE(state.selectable == available);
  REQUIRE(state.refreshable == available);
  REQUIRE(state.status.empty() == available);
}
