#pragma once

#include "qjsp/bytecode.hpp"
#include "qjsp/value.hpp"
#include <vector>

namespace qjsp {

struct Context;
struct Runtime;
struct Object;
struct String;
struct VarRef;

class RegInterpreter {
public:
  explicit RegInterpreter(Context *ctx) : ctx_(ctx) {}

  Value eval(FunctionBytecode *b);
  Value eval_source(const char *source, const char *filename = "<eval>");

private:
  Context *ctx_;
  Runtime *rt() const;
  Object *global_obj() const;

  Value call_bytecode(FunctionBytecode *b, Value this_obj, int argc,
                       Value *argv, VarRef **upvals);
  Value run_bytecode(FunctionBytecode *b, Value *regs, VarRef **upvals,
                     std::vector<VarRef *> *close_list = nullptr);

  Value get_field(Value obj, Atom name);
  void  put_field(Value obj, Atom name, Value val);

  struct CatchFrame {
    int exc_reg;
    int target_pc;
  };
  std::vector<CatchFrame> catch_stack_;
  Value pending_exception_ = kUndefined; // side channel for exception value
};

} // namespace qjsp
