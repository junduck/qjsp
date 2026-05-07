#pragma once

#include "atom.hpp"
#include <cstdint>
#include <vector>

namespace qjsp {

struct Runtime;

constexpr int kPropConfigurable = (1 << 0);
constexpr int kPropWritable = (1 << 1);
constexpr int kPropEnumerable = (1 << 2);
constexpr int kPropCWE = kPropConfigurable | kPropWritable | kPropEnumerable;

struct ShapeProperty {
  Atom atom = kAtomNull;
  int flags = kPropCWE;
};

/// Immutable property descriptor sequence. Shapes are owned by Runtime,
/// cached for deduplication, and shared across objects with identical
/// property layouts.

struct Shape {
  std::vector<ShapeProperty> entries;

  int size() const { return static_cast<int>(entries.size()); }
  int find(Atom atom) const;
};

} // namespace qjsp
