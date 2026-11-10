// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "bridge/join_interface.h"

#if defined(__linux__)
#  include <arpa/inet.h>
#  include <linux/netlink.h>
#  include <linux/rtnetlink.h>
#  include <net/if.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace bridge
{

namespace
{
constexpr std::string_view kScheme = "rist://";

// The host part of a RIST URL. libRIST's listen form prefixes the address with
// '@', and the authority ends at the port, the query or the path.
std::string host_of(const std::string& url)
{
  if (url.rfind(kScheme, 0) != 0) {
    return {};
  }
  std::size_t begin = kScheme.size();
  if (begin < url.size() && url[begin] == '@') {
    ++begin;
  }
  std::size_t end = url.find_first_of(":?/", begin);
  if (end == std::string::npos) {
    end = url.size();
  }
  return url.substr(begin, end - begin);
}

// IPv4 multicast is 224.0.0.0/4, i.e. a first octet in [224, 239]. Parsed by
// hand rather than with inet_pton: this file also builds for targets where
// arpa/inet.h does not exist.
bool is_multicast_v4(const std::string& host)
{
  unsigned parts[4] = {0, 0, 0, 0};
  int index = 0;
  int digits = 0;
  for (char c : host) {
    if (c >= '0' && c <= '9') {
      parts[index] = parts[index] * 10U + static_cast<unsigned>(c - '0');
      if (++digits > 3 || parts[index] > 255U) {
        return false;
      }
      continue;
    }
    if (c != '.' || digits == 0 || index == 3) {
      return false;
    }
    ++index;
    digits = 0;
  }
  return index == 3 && digits > 0 && parts[0] >= 224U && parts[0] <= 239U;
}
}  // namespace

std::string multicast_group(const std::string& url)
{
  const std::string host = host_of(url);
  return is_multicast_v4(host) ? host : std::string {};
}

std::string multicast_interface_of(const std::string& url)
{
  const auto query = url.find('?');
  if (query == std::string::npos) {
    return {};
  }
  constexpr std::string_view kKey = "miface=";
  std::size_t pos = query + 1;
  while (pos < url.size()) {
    if (url.compare(pos, kKey.size(), kKey) == 0) {
      const auto value = pos + kKey.size();
      const auto end = url.find('&', value);
      return url.substr(
          value, end == std::string::npos ? std::string::npos : end - value);
    }
    const auto next = url.find('&', pos);
    if (next == std::string::npos) {
      break;
    }
    pos = next + 1;
  }
  return {};
}

std::string with_multicast_interface(std::string url, const std::string& iface)
{
  if (iface.empty() || multicast_group(url).empty()
      || !multicast_interface_of(url).empty())
  {
    return url;
  }
  url += url.find('?') == std::string::npos ? '?' : '&';
  url += "miface=";
  url += iface;
  return url;
}

std::string without_multicast_interface(const std::string& url)
{
  const auto query = url.find('?');
  if (query == std::string::npos || multicast_interface_of(url).empty()) {
    return url;
  }
  std::string kept = url.substr(0, query + 1);
  std::size_t pos = query + 1;
  bool first = true;
  while (pos <= url.size()) {
    const auto end = url.find('&', pos);
    const std::string param = url.substr(
        pos, end == std::string::npos ? std::string::npos : end - pos);
    if (!param.empty() && param.rfind("miface=", 0) != 0) {
      if (!first) {
        kept += '&';
      }
      kept += param;
      first = false;
    }
    if (end == std::string::npos) {
      break;
    }
    pos = end + 1;
  }
  if (first) {
    kept.pop_back();  // no parameters left: drop the dangling '?'
  }
  return kept;
}

std::string join_warning(const std::string& listen_url)
{
  const std::string group = multicast_group(listen_url);
  if (group.empty() || !multicast_interface_of(listen_url).empty()) {
    return {};
  }
  return "the bridge listens on multicast group " + group
      + " with no join interface: set the bridge's join interface, or the "
        "group is never joined";
}

std::string detect_multicast_interface(const std::string& group)
{
#if defined(__linux__)
  if (!is_multicast_v4(group)) {
    return {};
  }
  in_addr dst {};
  if (::inet_pton(AF_INET, group.c_str(), &dst) != 1) {
    return {};
  }

  const int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (fd < 0) {
    return {};
  }

  // A route lookup for the group's own address, which is what `ip route get`
  // does: the answer names the interface the group would be reached through.
  struct request
  {
    nlmsghdr nlh;
    rtmsg rtm;
    char attrs[64];
  };
  request req {};
  req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
  req.nlh.nlmsg_type = RTM_GETROUTE;
  req.nlh.nlmsg_flags = NLM_F_REQUEST;
  req.nlh.nlmsg_seq = 1;
  req.rtm.rtm_family = AF_INET;
  req.rtm.rtm_dst_len = 32;
  auto* attr = reinterpret_cast<rtattr*>(req.attrs);
  attr->rta_type = RTA_DST;
  attr->rta_len = static_cast<unsigned short>(RTA_LENGTH(sizeof(in_addr)));
  std::memcpy(RTA_DATA(attr), &dst, sizeof(dst));
  req.nlh.nlmsg_len = static_cast<std::uint32_t>(NLMSG_ALIGN(req.nlh.nlmsg_len)
                                                 + RTA_ALIGN(attr->rta_len));

  if (::send(fd, &req, req.nlh.nlmsg_len, 0) < 0) {
    ::close(fd);
    return {};
  }

  char reply[4096];
  const auto received = ::recv(fd, reply, sizeof(reply), 0);
  ::close(fd);
  if (received < 0) {
    return {};
  }

  std::string iface;
  int remaining = static_cast<int>(received);
  for (auto* nlh = reinterpret_cast<nlmsghdr*>(reply); NLMSG_OK(nlh, remaining);
       nlh = NLMSG_NEXT(nlh, remaining))
  {
    if (nlh->nlmsg_type == NLMSG_ERROR) {
      return {};
    }
    auto* rtm = reinterpret_cast<rtmsg*>(NLMSG_DATA(nlh));
    int payload = RTM_PAYLOAD(nlh);
    for (auto* a = RTM_RTA(rtm); RTA_OK(a, payload); a = RTA_NEXT(a, payload)) {
      if (a->rta_type != RTA_OIF) {
        continue;
      }
      std::uint32_t index = 0;
      std::memcpy(&index,
                  RTA_DATA(a),
                  std::min<std::size_t>(RTA_PAYLOAD(a), sizeof(index)));
      char name[IF_NAMESIZE] = {};
      if (index != 0 && ::if_indextoname(index, name) != nullptr) {
        iface = name;
      }
      return iface;
    }
  }
  return iface;
#else
  (void)group;
  return {};
#endif
}

std::string resolve_join_interface(const std::string& listen_url,
                                   const std::string& configured,
                                   const std::string& fallback,
                                   const detect_fn& detect)
{
  if (multicast_group(listen_url).empty()) {
    return {};
  }
  // An interface already written into the listen URL is the operator's own
  // statement about the bridge and outranks anything derived here.
  const std::string in_url = multicast_interface_of(listen_url);
  if (!in_url.empty()) {
    return in_url;
  }
  if (!configured.empty()) {
    return configured;
  }
  const std::string detected = detect
      ? detect(multicast_group(listen_url))
      : detect_multicast_interface(multicast_group(listen_url));
  if (!detected.empty()) {
    return detected;
  }
  return fallback;
}

}  // namespace bridge
