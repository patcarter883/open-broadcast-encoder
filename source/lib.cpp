#include <exception>
#include <memory>
#include "lib.hpp"

#include <fmt/core.h>

library::library() noexcept try
    : name {fmt::format("{}", "open-broadcast-encoder")}
    , ui {std::make_unique<user_interface>()}
    , input_mode {sdp}
{
} catch(const std::exception& e) {
    std::terminate();
}
