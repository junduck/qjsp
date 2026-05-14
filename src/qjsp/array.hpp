#pragma once

#include "engine.hpp"
#include "object.hpp"
#include "value.hpp"
#include <cstdint>
#include <vector>

namespace qjsp {

struct Engine;

struct ArrayObject : Object {
  std::vector<Value> elements;

  static void setup(Engine *e);
  static Value create(Engine *e);

  Value get_elem(uint32_t idx) const { return idx < elements.size() ? elements[idx] : Value::undefined_(); }

  void set_elem(uint32_t idx, Value v) {
    if (idx >= elements.size())
      elements.resize(idx + 1);
    elements[idx] = v;
  }

  void gc_mark(std::vector<GCObjectHeader *> &worklist) override;
};

} // namespace qjsp
