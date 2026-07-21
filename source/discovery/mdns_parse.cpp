// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "discovery/mdns_parse.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace discovery
{
namespace
{
// DNS record types we care about.
constexpr uint16_t k_type_a = 1;
constexpr uint16_t k_type_ptr = 12;
constexpr uint16_t k_type_txt = 16;
constexpr uint16_t k_type_srv = 33;

// Read a DNS domain name at buf[pos], honouring 0xC0 compression pointers.
// `pos` is advanced to the first byte AFTER the name in the record stream (for
// a compressed name that is the two pointer bytes). Names are lower-cased and
// dot-joined. Returns false on a malformed name (bad length, runaway pointer).
bool read_name(const uint8_t* buf, std::size_t len, std::size_t& pos, std::string& out)
{
  out.clear();
  std::size_t p = pos;
  std::size_t after = 0;
  bool jumped = false;
  int hops = 0;
  while (true) {
    if (p >= len) {
      return false;
    }
    const uint8_t b = buf[p];
    if ((b & 0xC0) == 0xC0) {  // compression pointer
      if (p + 1 >= len) {
        return false;
      }
      const std::size_t target = ((static_cast<std::size_t>(b) & 0x3F) << 8) | buf[p + 1];
      if (!jumped) {
        after = p + 2;
        jumped = true;
      }
      p = target;
      if (++hops > 128) {
        return false;  // pointer loop
      }
      continue;
    }
    if ((b & 0xC0) != 0) {
      return false;  // reserved label type
    }
    if (b == 0) {  // root — end of name
      if (!jumped) {
        after = p + 1;
      }
      break;
    }
    if (p + 1 + b > len) {
      return false;
    }
    if (!out.empty()) {
      out.push_back('.');
    }
    for (std::size_t i = 0; i < b; ++i) {
      out.push_back(static_cast<char>(std::tolower(buf[p + 1 + i])));
    }
    p += 1 + b;
    if (++hops > 128) {
      return false;
    }
  }
  pos = after;
  return true;
}

uint16_t read_u16(const uint8_t* buf, std::size_t pos)
{
  return static_cast<uint16_t>((buf[pos] << 8) | buf[pos + 1]);
}

bool ends_with(const std::string& s, const std::string& suffix)
{
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}  // namespace

std::vector<uint8_t> build_ptr_query(std::string_view service_fqdn)
{
  std::vector<uint8_t> q;
  auto put16 = [&](uint16_t v) {
    q.push_back(static_cast<uint8_t>(v >> 8));
    q.push_back(static_cast<uint8_t>(v & 0xFF));
  };
  put16(0);  // transaction id (mDNS ignores)
  put16(0);  // flags: standard query
  put16(1);  // qdcount
  put16(0);  // ancount
  put16(0);  // nscount
  put16(0);  // arcount

  // QNAME: length-prefixed labels of the service fqdn.
  std::size_t i = 0;
  const std::string s(service_fqdn);
  while (i < s.size()) {
    std::size_t dot = s.find('.', i);
    if (dot == std::string::npos) {
      dot = s.size();
    }
    const std::size_t label = dot - i;
    if (label == 0 || label > 63) {
      break;
    }
    q.push_back(static_cast<uint8_t>(label));
    for (std::size_t k = i; k < dot; ++k) {
      q.push_back(static_cast<uint8_t>(s[k]));
    }
    i = dot + 1;
  }
  q.push_back(0);   // root label
  put16(k_type_ptr);
  put16(0x8001);    // QCLASS: IN + QU (request a unicast response)
  return q;
}

bool parse_response(const uint8_t* buf,
                    std::size_t len,
                    std::string_view service_fqdn,
                    std::vector<bridge>& out)
{
  if (buf == nullptr || len < 12) {
    return false;
  }
  const uint16_t qd = read_u16(buf, 4);
  const uint32_t total =
      static_cast<uint32_t>(read_u16(buf, 6)) + read_u16(buf, 8) + read_u16(buf, 10);

  std::size_t pos = 12;

  // Skip the question section.
  for (uint16_t i = 0; i < qd; ++i) {
    std::string qname;
    if (!read_name(buf, len, pos, qname)) {
      return false;
    }
    if (pos + 4 > len) {
      return false;
    }
    pos += 4;  // qtype + qclass
  }

  struct srv_rec
  {
    std::string owner;
    std::string target;
    int port = 0;
  };
  std::vector<srv_rec> srvs;
  std::map<std::string, std::string> a_records;  // name -> IPv4 dotted
  std::map<std::string, std::string> txts;       // owner -> joined TXT

  for (uint32_t r = 0; r < total; ++r) {
    std::string name;
    if (!read_name(buf, len, pos, name)) {
      return false;
    }
    if (pos + 10 > len) {
      return false;
    }
    const uint16_t type = read_u16(buf, pos);
    const uint16_t rdlen = read_u16(buf, pos + 8);
    const std::size_t rdata = pos + 10;
    if (rdata + rdlen > len) {
      return false;
    }

    if (type == k_type_srv && rdlen >= 6) {
      srv_rec rec;
      rec.owner = name;
      rec.port = read_u16(buf, rdata + 4);
      std::size_t tpos = rdata + 6;
      std::string target;
      if (read_name(buf, len, tpos, target)) {
        rec.target = target;
      }
      srvs.push_back(std::move(rec));
    } else if (type == k_type_a && rdlen == 4) {
      char ip[16];
      std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", buf[rdata], buf[rdata + 1], buf[rdata + 2],
                    buf[rdata + 3]);
      a_records[name] = ip;
    } else if (type == k_type_txt) {
      std::string joined;
      std::size_t t = rdata;
      while (t < rdata + rdlen) {
        const uint8_t l = buf[t];
        if (t + 1 + l > rdata + rdlen) {
          break;
        }
        if (!joined.empty()) {
          joined.push_back(';');
        }
        joined.append(reinterpret_cast<const char*>(buf + t + 1), l);
        t += 1 + l;
      }
      txts[name] = joined;
    }

    pos = rdata + rdlen;
  }

  // A bridge is any SRV whose owner sits under the queried service type.
  const std::string suffix = "." + std::string(service_fqdn);
  for (const auto& srv : srvs) {
    if (!ends_with(srv.owner, suffix)) {
      continue;
    }
    bridge b;
    b.name = srv.owner.substr(0, srv.owner.size() - suffix.size());
    b.port = srv.port;
    if (auto it = a_records.find(srv.target); it != a_records.end()) {
      b.host = it->second;
    }
    if (auto it = txts.find(srv.owner); it != txts.end()) {
      b.txt = it->second;
    }
    out.push_back(std::move(b));
  }
  return true;
}
}  // namespace discovery
