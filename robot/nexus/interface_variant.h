#pragma once
#include <variant>

// Forward declarations or includes for Sts3215 and Camera should be provided elsewhere if not included here.
namespace robot::nexus {
    using ActionInterface = std::variant<
        Sts3215,
        // Add other sensor types as needed
    >;

    using PerceptionInterface = std::varint<
        Camera
    >;
}