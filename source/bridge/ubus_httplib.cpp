// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <string>
#include <utility>

#include "bridge/ubus.h"

#include "lib/httplib_tls.h"
#include "lib/lib.h"

namespace
{
// Split "http://host:port/ubus" into a base httplib::Client accepts and the
// path. Returns {base, path}; path is "/" when the URL carries none.
auto split_url(const std::string& url) -> std::pair<std::string, std::string>
{
  const auto scheme = url.find("://");
  const auto path_at =
      scheme == std::string::npos ? url.find('/') : url.find('/', scheme + 3);
  if (path_at == std::string::npos) {
    return {url, "/"};
  }
  return {url.substr(0, path_at), url.substr(path_at)};
}
}  // namespace

// Build the socket end of the ubus client. The pair token is registered as a
// secret so it can never appear in a UI log pane (H2) -- it is sent in the
// request body, which is otherwise loggable by definition.
bridge::ubus_client::transport_fn bridge::make_ubus_transport(
    const std::string& pair_token)
{
  if (!pair_token.empty()) {
    secrets::register_secret(pair_token);
  }

  return [](const std::string& url,
            const std::string& body) -> std::pair<int, std::string>
  {
    const auto [base, path] = split_url(url);

    httplib::Client cli(base);
    // The CONNECTION timeout is the responsiveness guard: a bridge that is
    // unplugged or off fails to connect in seconds, which is the case that
    // matters for keeping the UI usable. The READ timeout has to cover the
    // slowest method instead, and `calibrate` measures every WAN leg and then
    // confirms the result -- about a minute. At 8s it gave up first and the
    // panel reported "the bridge did not answer" for a bridge that was answering
    // perfectly well. Control calls run on tracked background threads, so a long
    // read does not block the UI.
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(300, 0);
    cli.set_write_timeout(30, 0);

    const auto res = cli.Post(path, body, "application/json");
    if (!res) {
      // Status 0 is the no-response convention parse_response understands.
      return {0, ""};
    }
    return {res->status, res->body};
  };
}
