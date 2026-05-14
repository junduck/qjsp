#pragma once

#include "object.hpp"

namespace qjsp {

struct Engine;
struct Bytecode;

// ── CFunction signature ─────────────────────────────────────────────────
using CFunction = Value(Engine *e, Value this_val, int argc, const Value *argv);

// ── Callable (abstract) ─────────────────────────────────────────────────
//
//  Every object that can be invoked.  Subclasses override call().
//  There is no separate [[Construct]] virtual — the CTOR opcode handles
//  new-call semantics by pre-creating a plain object and calling call().
//  The call() receiver decides whether to use this_val or create its own.

struct Callable : Object {
  bool is_bytecode                                                           = false;
  virtual Value call(Engine *e, Value this_val, int argc, const Value *argv) = 0;
};

// ── CFunctionObj (native C function wrapper) ────────────────────────────

struct CFunctionObj : Callable {
  CFunction *fn = nullptr;

  static Value create(Engine *e, CFunction *fn, std::string_view name, int length);

  Value call(Engine *e, Value this_val, int argc, const Value *argv) override { return fn(e, this_val, argc, argv); }
};

// ── BFunctionObj (parsed JS function + closures) ────────────────────

struct BFunctionObj : Callable {
  Bytecode *bytecode = nullptr;
  std::vector<Value> var_refs;

  static Value create(Engine *e, Bytecode *bc);

  Value call(Engine *e, Value this_val, int argc, const Value *argv) override;

  void gc_mark(std::vector<GCObjectHeader *> &worklist) override;
  void gc_decref_refs() override;
  void gc_clear_refs() override;
};

// ── call() — virtual dispatch ───────────────────────────────────────────

Value call(Engine *e, Value func, Value this_val, int argc, const Value *argv);

// ── Function builtin ────────────────────────────────────────────────────

struct Function {
  static void setup(Engine *e);
};

} // namespace qjsp
