#pragma once

#include "gc.hpp"
#include "value.hpp"

namespace qjsp {

struct Bigint : RefCounted {
  int64_t data;

  static Bigint *allocate_raw(int64_t val);
  static Value create(int64_t val) { return Value::bigint_ptr(allocate_raw(val)); }
};
} // namespace qjsp
