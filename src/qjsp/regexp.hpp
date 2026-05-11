#pragma once

#include "gc.hpp"
#include "string.hpp"
#include "value.hpp"
#include <cstdint>

namespace qjsp {

struct RegExp {
  StrPrim *pattern;
  StrPrim *bytecode; // also contains the flags
};

} // namespace qjsp
