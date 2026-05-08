#pragma once

#include "atom.hpp"
#include <cstdint>
#include <memory>

namespace qjsp {

struct Runtime;

constexpr int kPropConfigurable = (1 << 0);
constexpr int kPropWritable     = (1 << 1);
constexpr int kPropEnumerable   = (1 << 2);
constexpr int kPropCWE          = kPropConfigurable | kPropWritable | kPropEnumerable;

struct ShapeProperty {
  Atom atom = kAtomNull;
  int flags = kPropCWE;
};

/// Immutable property descriptor sequence. Shapes are owned by Runtime,
/// cached for deduplication, and shared across objects with identical
/// property layouts.

struct Shape {
  std::unique_ptr<ShapeProperty[]> entries;
  uint32_t prop_count = 0;

  uint32_t size() const { return prop_count; }
  uint32_t find(Atom atom) const {
    for (uint32_t i = 0; i < prop_count; ++i)
      if (entries[i].atom == atom)
        return i;
    return prop_count; // sentinel: not found
  }
};

} // namespace qjsp
