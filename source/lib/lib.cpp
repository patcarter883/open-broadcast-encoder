// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pat Carter

#include "lib.h"

library::library() noexcept
    : is_running {false}
    , run_flag {std::make_shared<std::atomic<bool>>(false)}
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
