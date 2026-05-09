#pragma once

#include "gc.hpp"
#include "shape.hpp"
#include "value.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

namespace qjsp {

struct Runtime;
struct Context;
struct FunctionBytecode;

using CFunction = Value(Context *ctx, Value this_val, int argc, const Value *argv);

struct Property {
  Value value = Value::undefined_();
};

struct Object : GCObjectHeader {
  Value proto  = Value::undefined_();
  Shape *shape = nullptr;
  std::vector<Property> properties;

  uint16_t class_id = 0;
  bool extensible   = true;

  static Value create(Runtime *rt, Value proto, uint16_t class_id);

  // ── property access ──────────────────────────────────────────────────
  Value get_own(Atom atom) const;
  bool set_own(Runtime *rt, Atom atom, Value value, int flags = kPropCWE);
  bool define_own(Runtime *rt, Atom atom, Value value, int flags);
  bool has_own(Atom atom) const { return shape && shape->find(atom) < shape->size(); }
  Value get(Atom atom) const;

  void destroy(Runtime *rt);

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
  virtual Value call(Context *ctx, Value this_val, int argc, const Value *argv) = 0;
};

// ── CFunction ──────────────────────────────────────────────────────────

struct CFunctionObj : Callable {
  CFunction *fn      = nullptr;
  uint8_t   fn_length = 0;

  static Value create(Context *ctx, CFunction *fn, std::string_view name, int length);

  Value call(Context *ctx, Value this_val, int argc, const Value *argv) override {
    return fn(ctx, this_val, argc, argv);
  }
};

// ── BytecodeFunction ───────────────────────────────────────────────────

struct BytecodeFunction : Callable {
  FunctionBytecode *bytecode = nullptr;
  std::vector<Value> var_refs;

  bool is_bytecode() const final { return true; }

  static Value create(Runtime *rt, FunctionBytecode *bc);

  Value call(Context *ctx, Value this_val, int argc, const Value *argv) override;

  void gc_mark(std::vector<GCObjectHeader *> &worklist) override;
};

// ── calling ───────────────────────────────────────────────────────────

Value call(Context *ctx, Value func, Value this_val, int argc, const Value *argv);

// ── global ─────────────────────────────────────────────────────────────

void setup_global(Context *ctx, Object *global);

} // namespace qjsp
