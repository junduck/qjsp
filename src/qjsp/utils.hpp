#pragma once

#include <concepts>
#include <limits>

namespace qjsp {
template <std::integral T>
constexpr bool is_valid(T i) {
  if constexpr (std::unsigned_integral<T>) {
    return i != std::numeric_limits<T>::max();
  } else {
    // signed
    return i >= 0;
  }
}
} // namespace qjsp
