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

using CFunction = Value(Context *ctx, Value this_val, int argc, const Value *argv);

struct Property {
  Value value = Value::undefined_();
};

struct VarRef;

struct Object : GCObjectHeader {
  // ... existing fields

  Object *proto = nullptr;
  Shape *shape  = nullptr;
  std::vector<Property> properties;

  // Closure data (only valid for bytecode_function class)
  VarRef **var_refs = nullptr;

  // Class-specific data. Only valid for certain class_ids.
  struct CFunctionData {
    void *realm    = nullptr;
    CFunction *fn  = nullptr;
    uint8_t length = 0;
    int16_t magic  = 0;
  };
  union {
    CFunctionData cfunc;
    void *opaque;
  } u{};

  int var_ref_count = 0;
  uint16_t class_id = 0;
  bool extensible   = true;

  // ── factories ────────────────────────────────────────────────────────
  static Object *create(Runtime *rt, Object *proto, uint16_t class_id);
  static Object *make_cfunc(Context *ctx, CFunction *fn, std::string_view name, int length);

  // ── property access ──────────────────────────────────────────────────
  Value get_own(Atom atom) const;
  bool set_own(Runtime *rt, Atom atom, Value value, int flags = kPropCWE);
  bool define_own(Runtime *rt, Atom atom, Value value, int flags);
  bool has_own(Atom atom) const { return shape && shape->find(atom) >= 0; }
  Value get(Atom atom) const;

  void destroy(Runtime *rt);

  // ── GC ───────────────────────────────────────────────────────────────
  void gc_mark(std::vector<GCObjectHeader *> &worklist);
};

// ── calling ───────────────────────────────────────────────────────────

Value call(Context *ctx, Value func, Value this_val, int argc, const Value *argv);

// ── global ─────────────────────────────────────────────────────────────

void setup_global(Context *ctx, Object *global);

} // namespace qjsp
