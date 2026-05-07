#pragma once

#include "gc.hpp"
#include "string.hpp"
#include "value.hpp"
#include <cstdint>

namespace qjsp {

struct RegExp {
  String *pattern;
  String *bytecode; // also contains the flags
};

} // namespace qjsp
