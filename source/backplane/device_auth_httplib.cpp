// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

// The socket end of the device-authorization flow, kept out of device_auth.cpp
// the same way backplane_httplib.cpp is kept out of backplane.cpp: the logic
// stays free of httplib so it links into the tests.
//
// The device_code travels in the request BODY (both endpoints take no bearer
// token), so there is nothing to put in a header -- but it is registered as a
// secret by device_auth::start, which means httplib's own error strings cannot
// leak it into a log pane either.

#include <string>
#include <utility>

#include "backplane/device_auth.h"

#include "httplib.h"

backplane::device_auth::transport_fn make_device_auth_transport(
    const std::string& base_url)
{
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
