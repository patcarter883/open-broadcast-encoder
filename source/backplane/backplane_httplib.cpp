// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <string>
#include <utility>

#include "httplib.h"

#include "backplane/backplane.h"
#include "lib/lib.h"

namespace
{
struct base_endpoint
{
  std::string scheme_host;  // e.g. https://api.backplane.example.au
};
}  // namespace

// Build an httplib-backed transport for the given base URL. Registers the
// device token as a secret so it never reaches the UI log panes (M1.9).
backplane_client::transport_fn make_httplib_transport(const std::string& base_url,
                                                      const std::string& device_token)
{
  secrets::register_secret(device_token);
  return [base_url](const std::string& method,
                    const std::string& path,
                    const std::string& token,
                    const std::string& body)
             -> std::pair<int, std::string>
  {
    httplib::Client cli(base_url);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(8, 0);
    cli.set_write_timeout(5, 0);
    httplib::Headers headers;
    if (!token.empty()) {
      headers.emplace("Authorization", "Bearer " + token);
    }
    httplib::Result res;
    if (method == "DELETE") {
      res = cli.Delete(path, headers);
    } else {
      res = cli.Post(path, headers, body, "application/json");
    }
    if (!res) {
      return {0, ""};
    }
    return {res->status, res->body};
  };
}
