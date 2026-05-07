#pragma once

#include "qjsp/atom.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/parser.hpp"
#include "qjsp/value.hpp"
#include <cstdint>
#include <vector>

namespace qjsp {

struct Context;
struct Runtime;
struct Object;
struct String;

// Free bytecode helper functions.
uint32_t read_u32(const uint8_t *&pc);
int32_t read_i32(const uint8_t *&pc);
uint16_t read_u16(const uint8_t *&pc);
uint8_t read_u8(const uint8_t *&pc);
Atom read_atom(const uint8_t *&pc);

// ─── StackFrame ─────────────────────────────────────────────────────────────

struct StackFrame {
  FunctionBytecode *b = nullptr;
  const uint8_t *pc = nullptr;   // current bytecode pointer
  Value *sp = nullptr;           // operand stack top (next free slot)
  Value *var_buf = nullptr;      // local variables (argc + varcount)
  Value *arg_buf = nullptr;      // pointer to first argument on stack
  Value this_obj = kUndefined;
  StackFrame *prev_frame = nullptr;
};

// ─── Interpreter ────────────────────────────────────────────────────────────

class Interpreter {
public:
  explicit Interpreter(Context *ctx) : ctx_(ctx) {}

  /// Execute bytecode. Returns the result value.
  Value eval(FunctionBytecode *b);

  /// Evaluate a source string (lex → parse → interpret).
  Value eval_source(const char *source, const char *filename = "<eval>");

private:
  Context *ctx_;
  Runtime *rt() const;

  // ── operand stack ─────────────────────────────────────────────────────

  static constexpr int kStackSize = 8192;
  Value stack_[kStackSize]{};
  Value *stack_top_ = stack_ + kStackSize;

  // ── bytecode interpretation ───────────────────────────────────────────

  Value call_internal(FunctionBytecode *b, Value this_obj, int argc,
                      Value *argv);
  Value call_bytecode(FunctionBytecode *b, Value this_obj, int argc,
                      Value *argv);

  // ── variable resolution ───────────────────────────────────────────────

  Value get_var(Atom name);
  void put_var(Atom name, Value val, bool init);
  Value get_field(Value obj, Atom name);
  void put_field(Value obj, Atom name, Value val);

  // ── opcode handlers ───────────────────────────────────────────────────

  Value run_bytecode(FunctionBytecode *b, StackFrame *sf);

  StackFrame *current_frame_ = nullptr;
};

// Free function: resolve bytecode labels to absolute offsets.
void resolve_labels(FunctionBytecode *b, const FunctionDef *fd);

} // namespace qjsp
