#pragma once

#include "class.hpp"
#include "gc.hpp"
#include "shape.hpp"
#include "value.hpp"
#include <string_view>
#include <vector>

namespace qjsp {

struct Runtime;
struct Context;
struct FunctionBytecode;
struct Engine;

using CFunction = Value(Engine *e, Value this_val, int argc, const Value *argv);

struct Property {
  Value value = Value::undefined_();
};

struct Object : GCObjectHeader {
  Value proto  = Value::undefined_();
  Shape *shape = nullptr;
  std::vector<Property> properties;

  Builtin clsid   = Builtin::object;
  bool extensible = true;

  // ── resources ──────────────────────────────────────────────────
  static Value create(Engine *e, Value proto, Builtin clsid);
  void destroy(Engine *e);

  // ── property access ──────────────────────────────────────────────────
  Value get_own(Atom atom) const;
  bool set_own(Engine *e, Atom atom, Value value, int flags = kPropCWE);
  bool define_own(Engine *e, Atom atom, Value value, int flags);
  bool has_own(Atom atom) const { return shape && shape->find(atom) < shape->size(); }
  Value get(Atom atom) const;

  // ── calling ──────────────────────────────────────────────────────────
  virtual bool is_callable() const { return false; }

  // ── GC ───────────────────────────────────────────────────────────────
  void gc_mark(std::vector<GCObjectHeader *> &worklist) override;
  virtual ~Object() = default;
};

// ── Callable ───────────────────────────────────────────────────────────

struct Callable : Object {
  bool is_callable() const final { return true; }
  virtual bool is_bytecode() const { return false; }
  virtual Value call(Engine *e, Value this_val, int argc, const Value *argv) = 0;
};

// ── CFunction ──────────────────────────────────────────────────────────

struct CFunctionObj : Callable {
  CFunction *fn = nullptr;

  static Value create(Engine *e, CFunction *fn, std::string_view name, int length);

  Value call(Engine *e, Value this_val, int argc, const Value *argv) override { return fn(e, this_val, argc, argv); }
};

// ── BytecodeFunction ───────────────────────────────────────────────────

struct BytecodeFunction : Callable {
  FunctionBytecode *bytecode = nullptr;
  std::vector<Value> var_refs;

  bool is_bytecode() const final { return true; }

  static Value create(Engine *e, FunctionBytecode *bc);

  Value call(Engine *e, Value this_val, int argc, const Value *argv) override;

  void gc_mark(std::vector<GCObjectHeader *> &worklist) override;
};

// ── calling ───────────────────────────────────────────────────────────

Value call(Engine *e, Value func, Value this_val, int argc, const Value *argv);

} // namespace qjsp
