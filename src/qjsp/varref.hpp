#pragma once

#include "gc.hpp"
#include "stack_frame.hpp"
#include "value.hpp"
#include <cstdint>

namespace qjsp {

struct VarRef : GCObjectHeader {
  bool is_detached;
  bool is_lexical;
  bool is_const;

  Value *pvalue;

  union {
    Value value;
    struct {
      uint16_t var_ref_idx;
      StackFrame *stack_frame;
    };
  };
};

} // namespace qjsp
