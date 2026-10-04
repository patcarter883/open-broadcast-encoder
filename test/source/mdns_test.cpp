// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter
//
// The mDNS wire contract (DT-19): query encoding and response parsing. Packets are
// built byte by byte here, so the parsing is exercised against the real format --
// including compression pointers and hostile truncation.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "bridge/mdns.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
constexpr const char* k_instance = "rist2rist-11:22:33:44:55:66";
constexpr const char* k_fqdn =
    "rist2rist-11:22:33:44:55:66._obr-rist._udp.local";
constexpr const char* k_service_name = "_obr-rist._udp.local";
constexpr const char* k_host = "OpenWrt.lan";
constexpr std::size_t k_header = 12;

// Byte-level DNS packet builder: each test states the packet it means.
class builder
{
public:
  std::vector<std::uint8_t>& data() { return m_data; }
  std::size_t size() const { return m_data.size(); }

  void u8(int value) { m_data.push_back(static_cast<std::uint8_t>(value & 0xFF)); }
  void u16(int value)
  {
    u8(value >> 8);
    u8(value);
  }
  void u32(long value)
  {
    u8(static_cast<int>(value >> 24));
    u8(static_cast<int>(value >> 16));
    u8(static_cast<int>(value >> 8));
    u8(static_cast<int>(value));
  }
  void text(const std::string& s)
  {
    m_data.insert(m_data.end(), s.begin(), s.end());
  }

  // A name in uncompressed label form, returning the offset it started at.
  std::size_t name(const std::string& dotted)
  {
    const std::size_t start_at = m_data.size();
    std::size_t start = 0;
    while (start < dotted.size()) {
      const auto dot = dotted.find('.', start);
      const auto end = dot == std::string::npos ? dotted.size() : dot;
      u8(static_cast<int>(end - start));
      text(dotted.substr(start, end - start));
      start = end + 1;
    }
    u8(0);
    return start_at;
  }

  // A compression pointer to an absolute packet offset.
  void pointer(std::size_t offset) { u16(0xC000 | static_cast<int>(offset)); }

  // An RR whose owner name is written in full.
  void rr(const std::string& owner,
          int type,
          long ttl,
          const std::function<void()>& rdata)
  {
    name(owner);
    write_rr(type, ttl, rdata);
  }

  // An RR whose owner name is a compression pointer to `owner_offset`.
  void rr_at(std::size_t owner_offset,
             int type,
             long ttl,
             const std::function<void()>& rdata)
  {
    pointer(owner_offset);
    write_rr(type, ttl, rdata);
  }

private:
  void write_rr(int type, long ttl, const std::function<void()>& rdata)
  {
    u16(type);
    u16(1);  // class IN
    u32(ttl);
    const auto length_at = m_data.size();
    u16(0);
    const auto rdata_at = m_data.size();
    rdata();
    const auto length = m_data.size() - rdata_at;
    m_data[length_at] = static_cast<std::uint8_t>((length >> 8) & 0xFF);
    m_data[length_at + 1] = static_cast<std::uint8_t>(length & 0xFF);
  }

  std::vector<std::uint8_t> m_data;
};

// DNS counts live in a header, so it is written last, in front of the built body.
auto packet(builder& body, int questions, int answers) -> std::vector<std::uint8_t>
{
  std::vector<std::uint8_t> out;
  out.push_back(0);
  out.push_back(0);  // id
  out.push_back(0x84);
  out.push_back(0x00);  // flags: response, authoritative
  for (const int count : {questions, answers, 0, 0}) {
    out.push_back(static_cast<std::uint8_t>((count >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(count & 0xFF));
  }
  out.insert(out.end(), body.data().begin(), body.data().end());
  return out;
}

// One length-prefixed TXT string.
void txt(builder& p, const std::string& entry)
{
  p.u8(static_cast<int>(entry.size()));
  p.text(entry);
}

void a_record(builder& p, int a, int b, int c, int d)
{
  p.u8(a);
  p.u8(b);
  p.u8(c);
  p.u8(d);
}

void srv(builder& p, int port, const std::string& host)
{
  p.u16(0);  // priority
  p.u16(0);  // weight
  p.u16(port);
  p.name(host);
}
}  // namespace

TEST_CASE("the query asks for PTR of our service", "[mdns]")
{
  const auto query = bridge::mdns::encode_query();

  REQUIRE(query.size() > k_header);
  REQUIRE(query[1] == 0);  // mDNS ignores the id
  REQUIRE(query[3] == 0);  // a standard query, no recursion
  REQUIRE(query[5] == 1);  // exactly one question
  REQUIRE(query[11] == 0);

  const std::vector<std::uint8_t> body(query.begin() + k_header, query.end());
  REQUIRE(body[0] == 9);
  REQUIRE(std::string(reinterpret_cast<const char*>(&body[1]), 9) == "_obr-rist");
  REQUIRE(body[10] == 4);
  REQUIRE(std::string(reinterpret_cast<const char*>(&body[11]), 4) == "_udp");
  REQUIRE(body[15] == 5);
  REQUIRE(std::string(reinterpret_cast<const char*>(&body[16]), 5) == "local");
  REQUIRE(body[21] == 0);   // the root
  REQUIRE(body[22] == 0);   // type = PTR
  REQUIRE(body[23] == 12);
  REQUIRE(body[24] == 0);   // class = IN
  REQUIRE(body[25] == 1);
}

TEST_CASE("a full advertisement becomes one merged service", "[mdns]")
{
  builder p;
  p.rr(k_fqdn, bridge::mdns::k_type_srv, 120,
       [&p] { srv(p, 5000, k_host); });
  p.rr(k_fqdn, bridge::mdns::k_type_txt, 4500, [&p] {
    txt(p, "fingerprint=ab12cd34");
    txt(p, "api=1");
  });
  p.rr(k_host, bridge::mdns::k_type_a, 120, [&p] { a_record(p, 192, 168, 8, 1); });

  const auto services = bridge::mdns::parse_response(packet(p, 0, 3));

  REQUIRE(services.size() == 1);
  REQUIRE(services[0].instance == k_instance);
  REQUIRE(services[0].port == 5000);
  REQUIRE(services[0].host == k_host);
  REQUIRE(services[0].address == "192.168.8.1");
  REQUIRE(services[0].txt_value("fingerprint") == "ab12cd34");
  REQUIRE(services[0].txt_value("api") == "1");
  REQUIRE(services[0].txt_value("absent").empty());
}

TEST_CASE("names are resolved through compression pointers", "[mdns]")
{
  // This is the shape umdns actually emits: the PTR's rdata holds the instance name
  // in full, and every later record names it with a pointer back to it.
  builder p;
  std::size_t instance_at = 0;
  p.rr(k_service_name, bridge::mdns::k_type_ptr, 4500,
       [&p, &instance_at] { instance_at = p.name(k_fqdn); });

  // The pointer targets an offset in the finished packet, so the header is added.
  p.rr_at(instance_at + k_header, bridge::mdns::k_type_srv, 120,
          [&p] { srv(p, 5000, k_host); });
  p.rr_at(instance_at + k_header, bridge::mdns::k_type_txt, 4500,
          [&p] { txt(p, "fingerprint=ab12cd34"); });
  p.rr(k_host, bridge::mdns::k_type_a, 120, [&p] { a_record(p, 10, 0, 0, 7); });

  const auto services = bridge::mdns::parse_response(packet(p, 0, 4));

  REQUIRE(services.size() == 1);
  REQUIRE(services[0].instance == k_instance);
  REQUIRE(services[0].port == 5000);
  REQUIRE(services[0].address == "10.0.0.7");
  REQUIRE(services[0].txt_value("fingerprint") == "ab12cd34");
}

TEST_CASE("a service outside ours is ignored", "[mdns]")
{
  builder p;
  p.rr("_ipp._tcp.local", bridge::mdns::k_type_ptr, 4500,
       [&p] { p.name("hp._ipp._tcp.local"); });
  p.rr("hp._ipp._tcp.local", bridge::mdns::k_type_txt, 4500,
       [&p] { txt(p, "ty=HP"); });

  // A PTR alone would be filtered by the target; the TXT must be filtered too, or
  // every printer on the LAN would show up as a bridge.
  REQUIRE(bridge::mdns::parse_response(packet(p, 0, 2)).empty());
}

TEST_CASE("two bridges on the LAN are reported separately", "[mdns]")
{
  builder p;
  p.rr(k_fqdn, bridge::mdns::k_type_srv, 120, [&p] { srv(p, 5000, k_host); });
  p.rr("rist2rist-aa:bb:cc._obr-rist._udp.local", bridge::mdns::k_type_srv, 120,
       [&p] { srv(p, 5001, "other.lan"); });

  const auto services = bridge::mdns::parse_response(packet(p, 0, 2));

  REQUIRE(services.size() == 2);
  REQUIRE(services[0].port == 5000);
  REQUIRE(services[0].instance == k_instance);
  REQUIRE(services[1].port == 5001);
  REQUIRE(services[1].instance == "rist2rist-aa:bb:cc");
}

TEST_CASE("a TXT value without an equals is kept as a bare flag", "[mdns]")
{
  builder p;
  p.rr(k_fqdn, bridge::mdns::k_type_txt, 4500, [&p] {
    txt(p, "managed");
    txt(p, "fingerprint=x");
  });

  const auto services = bridge::mdns::parse_response(packet(p, 0, 1));

  REQUIRE(services.size() == 1);
  REQUIRE(services[0].txt.count("managed") == 1);
  REQUIRE(services[0].txt_value("managed").empty());
  REQUIRE(services[0].txt_value("fingerprint") == "x");
}

TEST_CASE("an advertisement without an address is still usable", "[mdns]")
{
  // A/AAAA can be absent from a given response; the instance and port still are.
  builder p;
  p.rr(k_fqdn, bridge::mdns::k_type_srv, 120, [&p] { srv(p, 5000, k_host); });

  const auto services = bridge::mdns::parse_response(packet(p, 0, 1));

  REQUIRE(services.size() == 1);
  REQUIRE(services[0].port == 5000);
  REQUIRE(services[0].address.empty());
}

TEST_CASE("an AAAA address is rendered as text", "[mdns]")
{
  builder p;
  p.rr(k_fqdn, bridge::mdns::k_type_srv, 120, [&p] { srv(p, 5000, k_host); });
  p.rr(k_host, bridge::mdns::k_type_aaaa, 120, [&p] {
    p.u16(0x2001);
    p.u16(0x0DB8);
    p.u16(0);
    p.u16(0);
    p.u16(0);
    p.u16(0);
    p.u16(0);
    p.u16(1);
  });

  const auto services = bridge::mdns::parse_response(packet(p, 0, 2));

  REQUIRE(services.size() == 1);
  REQUIRE(services[0].address == "2001:db8:0:0:0:0:0:1");
}

TEST_CASE("malformed packets are refused rather than read past", "[mdns]")
{
  SECTION("shorter than a header")
  {
    const std::vector<std::uint8_t> tiny{0, 0, 0};
    REQUIRE(bridge::mdns::parse_response(tiny).empty());
  }
  SECTION("a record truncated mid-rdata")
  {
    builder p;
    p.name(k_fqdn);
    p.u16(bridge::mdns::k_type_txt);
    p.u16(1);
    p.u32(4500);
    p.u16(64);  // claims 64 bytes of rdata, then supplies 2
    p.u8(1);
    p.u8('x');
    REQUIRE(bridge::mdns::parse_response(packet(p, 0, 1)).empty());
  }
  SECTION("a compression pointer into nowhere")
  {
    builder p;
    p.rr("_obr-rist._udp.local", bridge::mdns::k_type_ptr, 4500,
         [&p] { p.pointer(0x0FF0); });
    REQUIRE(bridge::mdns::parse_response(packet(p, 0, 1)).empty());
  }
  SECTION("a compression loop")
  {
    // A pointer whose target is the pointer itself must not spin forever.
    builder p;
    p.pointer(k_header);
    REQUIRE(bridge::mdns::parse_response(packet(p, 0, 1)).empty());
  }
  SECTION("a TXT string running past its rdata")
  {
    builder p;
    p.rr(k_fqdn, bridge::mdns::k_type_txt, 4500, [&p] {
      p.u8(200);  // claims 200 bytes inside a 2-byte record
      p.u8('x');
    });
    REQUIRE(bridge::mdns::parse_response(packet(p, 0, 1)).empty());
  }
  SECTION("a bad label length")
  {
    builder p;
    p.u8(0xFF);  // a reserved label type
    p.u16(bridge::mdns::k_type_txt);
    p.u16(1);
    p.u32(4500);
    p.u16(0);
    REQUIRE(bridge::mdns::parse_response(packet(p, 0, 1)).empty());
  }
}
