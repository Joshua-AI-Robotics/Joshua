#pragma once
#include <variant>

// Forward declarations or includes for Sts3215 and Camera should be provided elsewhere if not included here.
namespace robot::nexus {
    using InterfaceVariant = std::variant<
        Sts3215,
        Camera
        // Add other sensor types as needed
    >;
}