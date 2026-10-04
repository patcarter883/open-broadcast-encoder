// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The socket end of bridge reporting, kept out of reporting.cpp the same way
// backplane_httplib.cpp is kept out of backplane.cpp: the request/response
// shaping stays free of httplib so it links into the tests.

#include <string>
#include <utility>

#include "bridge/reporting.h"

#include "httplib.h"
#include "lib/lib.h"

// Build an httplib-backed transport for the numbered /v1 endpoints the reporter
// uses. The device token authorises every call, and is registered as a secret so
// it cannot reach a log pane (H2).
bridge::backplane_transport_fn make_bridge_reporter_transport(
    const std::string& base_url, const std::string& device_token)
{
  secrets::register_secret(device_token);
  return [base_url](const std::string& method,
                    const std::string& path,
                    const std::string& token,
                    const std::string& body) -> std::pair<int, std::string>
  {
    httplib::Client cli(base_url);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(8, 0);
    cli.set_write_timeout(5, 0);

    httplib::Headers headers;
    if (!token.empty())
    {
      headers.emplace("Authorization", "Bearer " + token);
    }

    httplib::Result res;
    if (method == "DELETE")
    {
      res = cli.Delete(path, headers);
    }
    else
    {
      res = cli.Post(path, headers, body, "application/json");
    }
    if (!res)
    {
      return {0, ""};
    }
    return {res->status, res->body};
  };
}
