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

  static void setup(Engine *e);

  // ── resources ──────────────────────────────────────────────────
  static Value create(Engine *e, Value proto, Builtin clsid);

  // ── property access ──────────────────────────────────────────────────
  Value get_own(Atom atom) const;
  bool set_own(Engine *e, Atom atom, Value value, int flags = kPropCWE);
  bool define_own(Engine *e, Atom atom, Value value, int flags);
  bool has_own(Atom atom) const { return shape && shape->find(atom) < shape->size(); }
  Value get(Atom atom) const;

  // ── calling ──────────────────────────────────────────────────────────
  virtual bool is_callable() const { return false; }

  // ── GC ───────────────────────────────────────────────────────────────
  //
  // These three virtuals implement the cycle-breaking trial-deletion
  // algorithm run by Engine::run_gc().  They are NOT called by the
  // fast path Engine::sweep_dead() — that path only inspects ref_count.
  //
  // === run_gc() protocol (see engine.cpp) ===
  //
  // Phase 1:  for each GC object, gc_refs = ref_count, is_marked = false.
  // Phase 2:  gc_decref_refs() on every object.
  // Phase 3:  DFS via gc_mark() starting from objects with gc_refs > 0.
  //           Objects that remain unmarked are dead (garbage cycles).
  // Phase 4:  gc_clear_refs() on every dead object to break inter-object
  //           references, then delete.
  //
  // Subclass contract: every override must call the base-class version
  // FIRST in gc_mark and gc_decref_refs, and LAST in gc_clear_refs.
  // Each override must traverse exactly the fields that hold references
  // to other GCObjectHeader-subclass objects.

  // ── gc_mark ─────────────────────────────────────────────────────────
  //
  // Purpose:  transitively mark live objects reachable from a root
  //           (an object whose gc_refs > 0 after Phase 2).
  //
  // Required behaviour:
  //   1. Set is_marked = true on self.
  //   2. For each field that refers to another GCObjectHeader* (proto,
  //      property values, array elements, var_refs targets, etc.):
  //      if the child exists and is not yet marked, mark it and push it
  //      onto the worklist for deferred traversal.
  //
  // Caveats:
  //   - Check is_marked BEFORE pushing — the same child may be reached
  //     through multiple paths; skipping already-marked children avoids
  //     quadratic worklist growth.
  //   - This method is only called during Phase 3 of run_gc().  It does
  //     not need to be reentrant or thread-safe.
  void gc_mark(std::vector<GCObjectHeader *> &worklist) override;

  // ── gc_decref_refs ──────────────────────────────────────────────────
  //
  // Purpose:  subtract internal (intra-heap) references from every
  //           reachable child's gc_refs counter so that after Phase 2,
  //           gc_refs reflects only external references.
  //
  // Required behaviour:
  //   For each field that refers to another GCObjectHeader*, decrement
  //   that child's gc_refs by 1.  This tells the trial-deletion
  //   algorithm "the parent's reference to this child is internal to
  //   the GC object set and must not prevent collection."
  //
  // Caveats:
  //   - Must decrement exactly once per outgoing edge.  If an object
  //     holds N references to the same child (e.g. proto + a property
  //     both point to X), decrement X.gc_refs N times.
  //   - Do NOT decrement for references to non-GCObjectHeader objects
  //     (StrPrim, VarRef, Bigint, Bytecode).  Those are managed purely
  //     by reference counting and are outside the GC object set.
  void gc_decref_refs() override;

  // ── gc_clear_refs ───────────────────────────────────────────────────
  //
  // Purpose:  break all outgoing references from a dead object so that
  //           the subsequent delete does not cascade into live objects.
  //
  // Required behaviour:
  //   Null out or clear every field that holds a reference to another
  //   GCObjectHeader*.  Typically: set proto to undefined, clear
  //   properties / elements / var_refs vectors, etc.
  //
  // Caveats:
  //   - Called on ALL dead objects BEFORE any dead object is deleted
  //     (see Phase 4 in run_gc).  This order ensures that even if A and
  //     B are mutually dead and reference each other, A's clear_refs
  //     does not access freed memory when B's clear_refs later runs.
  //   - After gc_clear_refs returns, the object must hold zero outgoing
  //     references to other GC objects.  The engine then calls delete,
  //     whose destructor sees only undefined / empty containers.
  //   - gc_clear_refs is NOT called by sweep_dead() — sweep_dead deals
  //     exclusively with objects whose ref_count is already 0, meaning
  //     no live object references them (and they cannot reference each
  //     other in a cycle either, because a cycle implies ref_count >= 1).
  void gc_clear_refs() override;
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
  void gc_decref_refs() override;
  void gc_clear_refs() override;
};

// ── calling ───────────────────────────────────────────────────────────

Value call(Engine *e, Value func, Value this_val, int argc, const Value *argv);

} // namespace qjsp
