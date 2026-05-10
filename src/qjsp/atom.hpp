#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace qjsp {

/// Atom — a lightweight index into the runtime string interning table.
/// kAtomNull (0) is the sentinel. Values are valid while the Runtime lives.
using Atom = uint32_t;

constexpr Atom kAtomNull    = 0;
constexpr Atom kInvalidAtom = UINT32_MAX;

} // namespace qjsp
