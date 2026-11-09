// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#pragma once

#include <string>

namespace obr
{

// The destination a RIST sender is pointed at.
struct rist_address
{
  std::string host;  // empty when the address names no host
  int port = 5000;   // 5000 unless a valid port is stated
};

// Parse an output address into host and port. Accepts every form an operator
// can type into the RIST Address field:
//
//   rist://host:20000?bandwidth=20000      scheme and query ignored
//   host:20000                             bare, no scheme
//   [2001:db8::1]:20000                    bracketed IPv6 with a port
//   2001:db8::1                            bare IPv6, host only
//   rist://@239.1.1.10:5100?miface=eth1     '@' kept (it is libRIST's listen
//                                          marker; the sender strips it)
//
// A port that is absent, non-numeric or out of range leaves the default.
rist_address parse_rist_address(const std::string& address);

}  // namespace obr
