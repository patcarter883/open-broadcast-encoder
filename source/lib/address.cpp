// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "lib/address.h"

#include <cctype>
#include <cstddef>

namespace
{

constexpr int default_port = 5000;

bool is_digits(const std::string& s)
{
  if (s.empty()) {
    return false;
  }
  for (char c : s) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
  }
  return true;
}

std::string trimmed(const std::string& s)
{
  std::size_t first = 0;
  std::size_t last = s.size();
  while (first < last &&
         std::isspace(static_cast<unsigned char>(s[first])) != 0) {
    ++first;
  }
  while (last > first &&
         std::isspace(static_cast<unsigned char>(s[last - 1])) != 0) {
    --last;
  }
  return s.substr(first, last - first);
}

int port_or_default(const std::string& stated)
{
  if (!is_digits(stated)) {
    return default_port;
  }
  try {
    int port = std::stoi(stated);
    if (port < 1 || port > 65535) {
      return default_port;
    }
    return port;
  } catch (...) {
    return default_port;
  }
}

std::size_t count_char(const std::string& s, char c)
{
  std::size_t n = 0;
  for (char x : s) {
    if (x == c) {
      ++n;
    }
  }
  return n;
}

}  // namespace

obr::rist_address obr::parse_rist_address(const std::string& address)
{
  rist_address out;
  std::string rest = trimmed(address);
  if (rest.empty()) {
    return out;
  }

  // A scheme is decoration here: only the host and port reach a socket. Cut it
  // at "://" rather than at the first ':' -- that is what made "rist://host:port"
  // parse as host "rist".
  auto scheme_end = rest.find("://");
  if (scheme_end != std::string::npos) {
    rest = rest.substr(scheme_end + 3);
  }

  // Path, query and fragment are not part of the destination.
  auto cut = rest.find_first_of("/?#");
  if (cut != std::string::npos) {
    rest = rest.substr(0, cut);
  }
  if (rest.empty()) {
    return out;
  }

  // User info, when present, precedes the host. A leading '@' is libRIST's
  // listen marker and belongs to the host, so it is not user info.
  if (rest.front() != '@') {
    auto at = rest.rfind('@');
    if (at != std::string::npos) {
      rest = rest.substr(at + 1);
    }
  }

  if (rest.empty()) {
    return out;
  }

  // Bracketed IPv6: [addr]:port, or [addr] on its own.
  if (rest.front() == '[') {
    auto close = rest.find(']');
    if (close == std::string::npos) {
      // Unterminated bracket: treat the whole thing as the host so the sender
      // fails on a resolvable-looking address rather than silently retargeting.
      out.host = rest;
      return out;
    }
    out.host = rest.substr(0, close + 1);
    auto after = rest.substr(close + 1);
    if (!after.empty() && after.front() == ':') {
      out.port = port_or_default(after.substr(1));
    }
    return out;
  }

  // One colon separates host and port. More than one is a bare IPv6 literal,
  // which carries no port (the bracketed form is the way to state both).
  if (count_char(rest, ':') == 1) {
    auto colon = rest.find(':');
    out.host = rest.substr(0, colon);
    out.port = port_or_default(rest.substr(colon + 1));
    return out;
  }

  out.host = rest;
  return out;
}
