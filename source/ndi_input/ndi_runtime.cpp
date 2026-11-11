// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <initializer_list>

#include "ndi_input/ndi_runtime.h"

#include <gst/gst.h>

auto ndi_runtime_available() -> bool
{
  // ndisrc and ndisink ship in the same plugin, so either factory proves the
  // runtime is present. A registry lookup does not touch the network.
  for (const char* factory_name : {"ndisrc", "ndisink"}) {
    GstElementFactory* factory = gst_element_factory_find(factory_name);
    if (factory == nullptr) {
      continue;
    }
    gst_object_unref(factory);
    return true;
  }
  return false;
}
