#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace qjsp {

/// Atom — a lightweight index into the runtime string interning table.
/// kAtomNull (0) is the sentinel. Values are valid while the Runtime lives.
using Atom = uint32_t;

constexpr Atom kAtomNull    = 0;
constexpr Atom kAtomInvalid = UINT32_MAX;

namespace WellKnown {
enum WellKnownAtoms : uint16_t {
  empty_string = 0,
  prototype,
  constructor,
  length,
  name,
  toString,
  valueOf,
  eval,
  undefined,
  of,
  __proto__,

  // Symbol
  symbol_iterator,
  symbol_asyncIterator,
  symbol_toPrimitive,
  symbol_toStringTag,
  symbol_hasInstance,
  symbol_species,

  // Sentinel
  Count
};

constexpr inline auto AtomBegin   = static_cast<uint16_t>(empty_string);
constexpr inline auto SymbolBegin = static_cast<uint16_t>(symbol_iterator);

constexpr inline auto names = []() constexpr {
  std::array<std::string_view, Count> a{"empty_string",
                                        "prototype",
                                        "constructor",
                                        "length",
                                        "name",
                                        "toString",
                                        "valueOf",
                                        "eval",
                                        "undefined",
                                        "of",
                                        "__proto__",
                                        "symbol_iterator",
                                        "symbol_asyncIterator",
                                        "symbol_toPrimitive",
                                        "symbol_toStringTag",
                                        "symbol_hasInstance",
                                        "symbol_species"};

  return a;
}();
} // namespace WellKnown

} // namespace qjsp
