#include "lib.h"

library::library() noexcept
    : is_running {false}
    , run_flag {std::make_shared<std::atomic<bool>>(false)}
{
}