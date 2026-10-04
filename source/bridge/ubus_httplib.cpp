// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <string>
#include <utility>

#include "bridge/ubus.h"
#include "lib/lib.h"

#include "httplib.h"

namespace
{
// Split "http://host:port/ubus" into a base httplib::Client accepts and the path.
// Returns {base, path}; path is "/" when the URL carries none.
auto split_url(const std::string& url) -> std::pair<std::string, std::string>
{
  const auto scheme = url.find("://");
  const auto path_at = scheme == std::string::npos
                           ? url.find('/')
                           : url.find('/', scheme + 3);
  if (path_at == std::string::npos) {
    return {url, "/"};
  }
  return {url.substr(0, path_at), url.substr(path_at)};
}
}  // namespace

// Build the socket end of the ubus client. The pair token is registered as a secret
// so it can never appear in a UI log pane (H2) -- it is sent in the request body,
// which is otherwise loggable by definition.
bridge::ubus_client::transport_fn bridge::make_ubus_transport(
    const std::string& pair_token)
{
  if (!pair_token.empty()) {
    secrets::register_secret(pair_token);
  }

  return [](const std::string& url,
            const std::string& body) -> std::pair<int, std::string> {
    const auto [base, path] = split_url(url);

    httplib::Client cli(base);
    // A bridge on a LAN answers fast or not at all: short timeouts keep the UI
    // responsive when a bridge has been unplugged.
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(8, 0);
    cli.set_write_timeout(5, 0);

    const auto res = cli.Post(path, body, "application/json");
    if (!res) {
      // Status 0 is the no-response convention parse_response understands.
      return {0, ""};
    }
    return {res->status, res->body};
  };
}
