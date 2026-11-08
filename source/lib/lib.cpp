// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include <algorithm>

#include "lib.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace
{
constexpr std::size_t k_encoder_name_max = 64;
}  // namespace

auto default_encoder_name() -> std::string
{
  char buf[256] {};
  std::string name;
#ifdef _WIN32
  DWORD len = static_cast<DWORD>(sizeof(buf));
  if (::GetComputerNameA(buf, &len) != 0 && len > 0) {
    name.assign(buf, len);
  }
#else
  if (::gethostname(buf, sizeof(buf) - 1) == 0 && buf[0] != '\0') {
    name = buf;
  }
#endif
  if (name.empty()) {
    return "encoder";
  }
  name.resize(std::min(name.size(), k_encoder_name_max));
  return name;
}

library::library() noexcept
    : is_running {false}
    , run_flag {std::make_shared<std::atomic<bool>>(false)}
    , encoder_name {default_encoder_name()}
{
}

library::~library()
{
  is_running = false;
  preview_running = false;
  if (run_flag) {
    *run_flag = false;
  }
  if (preview_thread.joinable()) {
    preview_thread.join();
  }
  for (auto& t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }
}

// ---------------------------------------------------------------------------
// Secret hygiene (FIXPLAN M1.9)
// ---------------------------------------------------------------------------

namespace secrets
{
namespace
{
std::mutex g_mutex;
std::vector<std::string> g_secrets;
constexpr std::size_t k_min_secret_len = 4;

void mask_param(std::string& text, const std::string& key)
{
  std::size_t pos = 0;
  while ((pos = text.find(key, pos)) != std::string::npos) {
    const std::size_t begin = pos + key.size();
    std::size_t end = begin;
    while (end < text.size() && text[end] != '&' && text[end] != ' '
           && text[end] != '"' && text[end] != '\'' && text[end] != '\n')
    {
      ++end;
    }
    if (end > begin) {
      text.replace(begin, end - begin, "***");
    }
    pos = begin;
  }
}
}  // namespace

void register_secret(const std::string& value)
{
  if (value.size() < k_min_secret_len) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  for (const std::string& s : g_secrets) {
    if (s == value) {
      return;
    }
  }
  g_secrets.push_back(value);
}

auto redact(std::string text) -> std::string
{
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const std::string& secret : g_secrets) {
      std::size_t pos = 0;
      while ((pos = text.find(secret, pos)) != std::string::npos) {
        text.replace(pos, secret.size(), "***");
        pos += 3;
      }
    }
  }
  mask_param(text, "secret=");
  mask_param(text, "streamid=");
  mask_param(text, "psk=");
  mask_param(text, "token=");
  return text;
}
}  // namespace secrets
