#pragma once

#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace qjsp {
template <std::integral T>
constexpr bool is_sentinel(T val) {
  if constexpr (std::unsigned_integral<T>) {
    return val == std::numeric_limits<T>::max();
  } else {
    // signed
    return val < 0;
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

inline uint32_t string_to_index(std::string_view sv) {
  if (sv.empty() || sv.size() > 10)
    return sentinel(uint32_t{});
  uint32_t acc = 0;
  for (size_t i = 0; i < sv.size(); ++i) {
    unsigned d = static_cast<unsigned>(sv[i] - '0');
    if (d > 9)
      return sentinel(uint32_t{});
    acc = acc * 10 + d;
  }
  return acc;
}
} // namespace qjsp
