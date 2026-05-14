#pragma once

#include "qjsp/bytecode.hpp"
#include "qjsp/value.hpp"
#include <vector>

namespace qjsp {

struct Engine;
struct Object;
struct StrPrim;
struct VarRef;
struct ArrayObject;

class RegInterpreter {
public:
  explicit RegInterpreter(Engine *e) : e_(e) {}

  Value eval(Bytecode *b);
  Value call_bytecode(Bytecode *b, Value this_obj, int argc, const Value *argv, VarRef **upvals);
  Value eval_source(const char *source, const char *filename = "<eval>");

private:
  Engine *e_;
  Object *global_obj() const;

  Value run_bytecode(Bytecode *b, Value *regs, VarRef **upvals, std::vector<VarRef *> *close_list = nullptr);

  Value get_field(Value obj, Atom name);
  void put_field(Value obj, Atom name, Value val);

  struct CatchFrame {
    int exc_reg;
    int target_pc;
    const Bytecode *bytecode;
  };
  std::vector<CatchFrame> catch_stack_;
  std::vector<int> return_stack_; // GOSUB/RET return addresses
  Value pending_exception_ = Value::undefined_();

  // for-in iteration state
  struct ForInState {
    Object *obj               = nullptr;
    const struct Shape *shape = nullptr;
    int current_index         = 0;
  };
  std::vector<ForInState> for_in_states_;

  // for-of iteration state (array elements)
  struct ForOfState {
    ArrayObject *arr       = nullptr;
    uint32_t current_index = 0;
  };
  std::vector<ForOfState> for_of_states_;
};

} // namespace qjsp
