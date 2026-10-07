// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include "httplib.h"

// cpp-httplib has NO TLS unless CPPHTTPLIB_OPENSSL_SUPPORT is defined, and in that
// state it refuses an https URL outright: the scheme check throws
// std::invalid_argument("'https' scheme is not supported.") and the SSL client is
// never created. Every production endpoint this app talks to is https -- the
// backplane API and each relay node's control_url -- so such a build cannot reach
// production at all while looking perfectly healthy, and the failure surfaces as an
// exception thrown on a background worker thread.
//
// Hence: ONE definition of the rule, checked at compile time. Include THIS header
// rather than httplib.h directly, so the guard cannot be bypassed and so every
// translation unit sees the same client layout (the macro changes it, so a TU that
// misses it is an ODR violation, not merely a missing feature).
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#error "httplib must be built with TLS (link httplib_ssl): every endpoint is https."
#endif
