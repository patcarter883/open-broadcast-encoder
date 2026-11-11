// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

// True when this machine's GStreamer registry carries the NDI elements, i.e.
// the NDI runtime is installed. Whether any sources are on the network is a
// separate question: a machine with the runtime and no senders is available.
auto ndi_runtime_available() -> bool;
