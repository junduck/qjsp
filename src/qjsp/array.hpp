#pragma once

#include "object.hpp"
#include "value.hpp"
#include <cstdint>
#include <vector>

namespace qjsp {

struct Runtime;
struct Context;

struct ArrayObject : Object {
  std::vector<Value> elements;

  static Value create(Runtime *rt, Value proto = Value::undefined_());

  Value get_elem(uint32_t idx) const {
    return idx < elements.size() ? elements[idx] : Value::undefined_();
  }

  void set_elem(uint32_t idx, Value v) {
    if (idx >= elements.size())
      elements.resize(idx + 1);
    elements[idx] = v;
  }

  void gc_mark(std::vector<GCObjectHeader *> &worklist) override;
};

// Wire up Array.prototype[Symbol.iterator]
void init_array_prototype(Context *ctx);

} // namespace qjsp
