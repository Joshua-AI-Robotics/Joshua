#pragma once
#include <cmath>
#include <limits>
#include <type_traits>

#include "absl/status/statusor.h"
namespace ros2_utils {
// Check before casting: out-of-range floating-to-integer conversion is undefined.
// Integer output is exact; fractional values are never silently truncated.
template <typename T>
absl::StatusOr<T> CheckedNumber(long double value) {
  if (!std::isfinite(value)) return absl::InvalidArgumentError("Non-finite numeric value");
  if constexpr (std::is_integral_v<T>) {
    const long double bound = std::ldexp(1.0L, std::numeric_limits<T>::digits);
    const long double lower = std::is_signed_v<T> ? -bound : 0.0L;
    if (value < lower || value >= bound || std::trunc(value) != value)
      return absl::OutOfRangeError("Value is fractional or outside the integer message range");
  } else {
    if (value < -std::numeric_limits<T>::max() || value > std::numeric_limits<T>::max())
      return absl::OutOfRangeError("Value exceeds floating-point range");
  }
  const T result = static_cast<T>(value);
  if constexpr (std::is_floating_point_v<T>) {
    if (value != 0 && result == 0) return absl::OutOfRangeError("Floating-point underflow");
  }
  return result;
}

}  // namespace ros2_utils
