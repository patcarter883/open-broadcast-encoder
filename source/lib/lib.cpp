#include "lib.h"

library::library() noexcept try
    : is_running {std::atomic<bool>(false)}
{}
catch (const std::exception& e) {
  std::terminate();
}