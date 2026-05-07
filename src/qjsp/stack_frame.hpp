#pragma once

#include "value.hpp"
#include <cstdint>

namespace qjsp {

struct VarRef;

constexpr int kModeStrict = (1 << 0);
constexpr int kModeAsync = (1 << 2);
constexpr int kModeBacktraceBarrier = (1 << 3);

struct StackFrame {
  StackFrame *prev_frame;
  Value cur_func;
  Value *arg_buf;
  Value *var_buf;
  VarRef **var_refs;
  const uint8_t *cur_pc;
  int arg_count;
  int js_mode;
  Value *cur_sp;
};

} // namespace qjsp
