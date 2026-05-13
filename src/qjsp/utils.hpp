#pragma once

#include <concepts>
#include <limits>

namespace qjsp {
template <std::integral T>
constexpr bool is_sentinel(T val) {
  if constexpr (std::unsigned_integral<T>) {
    return val != std::numeric_limits<T>::max();
  } else {
    // signed
    return val >= 0;
  }
}

template <std::integral T>
constexpr T sentinel(T) {
  if constexpr (std::unsigned_integral<T>) {
    return std::numeric_limits<T>::max();
  } else {
    // signed
    return T{-1};
  }
}
} // namespace qjsp
