#pragma once

#include <scope_guard.hpp>

// Helper macros to generate a unique name using the line number
#define CONCATENATE_DETAIL(x, y) x##y
#define CONCATENATE(x, y) CONCATENATE_DETAIL(x, y)
#define MAKE_UNIQUE_NAME(base) CONCATENATE(base, __LINE__)

// Macro to create the scope guard
#define SCOPE_GUARD_CAPTURE(body, ...) \
    const auto MAKE_UNIQUE_NAME(guard) = sg::make_scope_guard([##__VA_ARGS__]() noexcept { body; })