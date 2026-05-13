#pragma once

#include "object.hpp"
#include "string.hpp"
#include <memory>

namespace re2 {
class RE2;
}

namespace qjsp {

struct Engine;

struct RegExpObj : Object {
  std::unique_ptr<re2::RE2> regex;
  uint8_t flags  = 0;
  int last_index = 0;

  static Value create(Engine *e, StrPrim *pattern, StrPrim *flags_str);
};

void init_regexp_prototype(Engine *e);

} // namespace qjsp
