// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "net/adapters.h"

#if defined(_WIN32) || defined(_WIN64)
#  include <cstring>

#  include <winsock2.h>

#  include <windows.h>
#  include <iphlpapi.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <ifaddrs.h>
#  include <net/if.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#endif

namespace net
{
namespace
{
// An entry already carrying this name, or nullptr. Adapters arrive once per
// address family, so several records collapse into one entry.
auto find_by_name(std::vector<adapter>& list, const std::string& name)
    -> adapter*
{
  auto it = std::find_if(list.begin(),
                         list.end(),
                         [&name](const adapter& a) { return a.name == name; });
  return it == list.end() ? nullptr : &*it;
}
}  // namespace

auto is_ipv4_literal(const std::string& text) -> bool
{
  in_addr addr {};
  return inet_pton(AF_INET, text.c_str(), &addr) == 1;
}

#if defined(_WIN32) || defined(_WIN64)

namespace
{
// The adapter's name as the OS shows it: the friendly name ("Ethernet 2") when
// it has one, else the internal name, which is always present.
auto adapter_name(const IP_ADAPTER_ADDRESSES* entry) -> std::string
{
  const wchar_t* friendly = entry->FriendlyName;
  if (friendly != nullptr && friendly[0] != L'\0') {
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, friendly, -1, nullptr, 0, nullptr, nullptr);
    if (size > 1) {
      std::string name(static_cast<std::size_t>(size - 1), '\0');
      WideCharToMultiByte(
          CP_UTF8, 0, friendly, -1, name.data(), size, nullptr, nullptr);
      return name;
    }
  }
  // AdapterName is a narrow string and is always present.
  return entry->AdapterName != nullptr ? std::string(entry->AdapterName)
                                       : std::string{};
}
}  // namespace

auto list() -> std::vector<adapter>
{
  std::vector<adapter> adapters;
  ULONG size = 16 * 1024;
  std::vector<std::uint8_t> buffer;
  ULONG status = ERROR_BUFFER_OVERFLOW;
  for (int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW;
       ++attempt) {
    buffer.assign(size, 0);
    status = GetAdaptersAddresses(
        AF_INET,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
            | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr,
        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
        &size);
  }
  if (status != NO_ERROR) {
    return adapters;
  }
  for (auto* entry = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
       entry != nullptr;
       entry = entry->Next)
  {
    adapter record;
    record.name = adapter_name(entry);
    if (record.name.empty() || find_by_name(adapters, record.name) != nullptr) {
      continue;
    }
    record.up = entry->OperStatus == IfOperStatusUp;
    for (auto* unicast = entry->FirstUnicastAddress; unicast != nullptr;
         unicast = unicast->Next)
    {
      const auto* sa =
          reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
      if (sa == nullptr || sa->sin_family != AF_INET) {
        continue;
      }
      char text[INET_ADDRSTRLEN] = {};
      if (inet_ntop(AF_INET, &sa->sin_addr, text, sizeof(text)) != nullptr) {
        record.ipv4 = text;
        break;
      }
    }
    adapters.push_back(std::move(record));
  }
  return adapters;
}

#else

auto list() -> std::vector<adapter>
{
  std::vector<adapter> adapters;
  ifaddrs* first = nullptr;
  if (getifaddrs(&first) != 0) {
    return adapters;
  }
  for (ifaddrs* entry = first; entry != nullptr; entry = entry->ifa_next) {
    if (entry->ifa_name == nullptr || entry->ifa_addr == nullptr) {
      continue;
    }
    const std::string name = entry->ifa_name;
    adapter* record = find_by_name(adapters, name);
    if (record == nullptr) {
      adapters.push_back(adapter {name, {}, false});
      record = &adapters.back();
    }
    // Up is a property of the device, reported on every one of its addresses.
    record->up = record->up || (entry->ifa_flags & IFF_UP) != 0;
    if (record->ipv4.empty() && entry->ifa_addr->sa_family == AF_INET) {
      char text[INET_ADDRSTRLEN] = {};
      const auto* sa = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
      if (inet_ntop(AF_INET, &sa->sin_addr, text, sizeof(text)) != nullptr) {
        record->ipv4 = text;
      }
    }
  }
  freeifaddrs(first);
  return adapters;
}

#endif

auto miface_for(const std::vector<adapter>& adapters,
                const std::string& name,
                bind_style style) -> std::string
{
  if (name.empty()) {
    return {};
  }
  for (const adapter& a : adapters) {
    if (a.name != name) {
      continue;
    }
    // A device bind names the device; the address bind names the source
    // address, and an adapter without one cannot carry the leg.
    return style == bind_style::device_name ? a.name : a.ipv4;
  }
  if (is_ipv4_literal(name)) {
    return name;
  }
  return {};
}
}  // namespace net
