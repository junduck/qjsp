#include "qjsp/function.hpp"
// ============================================================================
//  Builtin Class Setup Template  —  ARRAY
// ============================================================================
//
//  This file is the canonical template for implementing a builtin class.
//  Every future builtin (Function, Error, RegExp, …) should follow the same
//  5‑stage pattern documented in ::setup() below.
//
//  Prerequisites for adding a new builtin:
//   1. Add an entry to the Builtin enum in class.hpp.
//   2. Enlarge builtin_protos[] in Engine if needed.
//   3. Ensure the parent class is already set up (Object is the root).
//   4. WellKnown atoms "prototype" and "constructor" are pre‑interned.
//
//  The CTOR opcode contract:
//   When the interpreter executes `new Xxx(...)`:
//     a. It reads Xxx["prototype"] — MUST be set (Stage 4).
//     b. Creates a fresh plain object with that prototype.
//     c. Calls Xxx.call(this = fresh_obj, …).
//     d. If the return value is an object, uses it; else uses fresh_obj.
//   Therefore every constructor MUST (A) set ctor["prototype"] and
//   (B) return an object of the proper specialised type (ArrayObject,
//   RegExpObj, …) so that step (d) picks it up.  The pre‑created
//   fresh_obj is discarded and collected by GC — this is by design.
//
//  Caveats:
//   • ::create() uses engine->get_proto(Builtin::array).  That requires
//     ::setup() to have already run, because Stage 1 populates the slot.
//   • The constructor returns a *new* ArrayObject regardless of this_val.
//     Never modify this_val as a side effect (it came from CTOR and may
//     have nothing to do with arrays).
//   • The ctor itself is a CFunctionObj, which is an Object subclass
//     tracked by GC.  Its proto chain is currently plain Object.prototype
//     (there is no Function.prototype yet).
//   • Interned atoms ("prototype", "constructor") are long‑lived because
//     atoms are owned by Engine::atom_table.  Using e‑>intern() inside
//     ::setup() is safe and idiomatic.
// ============================================================================

#include "qjsp/array.hpp"
#include "qjsp/class.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/object.hpp"

namespace qjsp {

// ─── Constructor ─────────────────────────────────────────────────────────────
//
//  Signature:  Array(...items)
//
//  Called both by `new Array(...)` (via CTOR opcode) and by bare
//  `Array(...)` (via CALL opcode).  The CTOR machinery (see file‑top
//  comment) pre‑creates a plain object with Array.prototype as its
//  __proto__ and passes it as this_val, then uses the return value
//  if it is an object.  Because we always return a new ArrayObject
//  the pre‑created object is harmless garbage that GC collects later.
//
//  For a bare call `Array(...)` this_val is undefined and the same
//  code path works identically.

static Value array_constructor(Engine *e, Value, int argc, const Value *argv) {
  Value arr = ArrayObject::create(e);
  auto *a   = static_cast<ArrayObject *>(arr.as<Object>());
  for (int i = 0; i < argc; i++)
    a->elements.push_back(argv[i]);
  return arr;
}

// ─── Prototype methods ───────────────────────────────────────────────────────
//
//  Each method is a CFunctionObj created during ::setup() (Stage 2) and
//  installed as an own property of Array.prototype.  They are static‑linked
//  to a C function and are *not* called through virtual dispatch.

static Value array_iterator_next(Engine *e, Value this_val, int, const Value *) {
  auto *iter = this_val.as<Object>();
  if (!iter)
    return Value::undefined_();

  uint32_t idx = 0;
  Value idx_v  = iter->get_own(e->intern("_idx"));
  if (idx_v.is_int32())
    idx = static_cast<uint32_t>(idx_v.as_int32());

  Value arr_v = iter->get_own(e->intern("_arr"));
  if (!arr_v.is_object())
    goto done;
  {
    auto *o = arr_v.as<Object>();
    if (o->clsid != Builtin::array)
      goto done;
    auto *arr = static_cast<ArrayObject *>(o);
    if (idx >= arr->elements.size())
      goto done;
    Value r  = Object::create(e, e->get_proto(Builtin::object), Builtin::object);
    auto *ro = r.as<Object>();
    ro->set_own(e, e->intern("value"), arr->elements[idx]);
    ro->set_own(e, e->intern("done"), Value::bool_(false));
    iter->set_own(e, e->intern("_idx"), Value::int32(static_cast<int32_t>(idx + 1)));
    return r;
  }
done: {
  Value r = Object::create(e, e->get_proto(Builtin::object), Builtin::object);
  r.as<Object>()->set_own(e, e->intern("done"), Value::bool_(true));
  r.as<Object>()->set_own(e, e->intern("value"), Value::undefined_());
  return r;
}
}

static Value array_values(Engine *e, Value this_val, int, const Value *) {
  Value iter = Object::create(e, e->get_proto(Builtin::object), Builtin::object);
  auto *o    = iter.as<Object>();
  o->set_own(e, e->intern("_arr"), this_val);
  o->set_own(e, e->intern("_idx"), Value::int32(0));
  o->set_own(e, e->intern("next"), CFunctionObj::create(e, array_iterator_next, "next", 0));
  return iter;
}

static Value array_push(Engine *e, Value this_val, int argc, const Value *argv) {
  auto *arr = this_val.as<ArrayObject>();
  if (!arr)
    return Value::undefined_();
  for (int i = 0; i < argc; i++)
    arr->elements.push_back(argv[i]);
  return Value::int32(static_cast<int32_t>(arr->elements.size()));
}

// ─── GC overrides ────────────────────────────────────────────────────────────
//
//  Every Object subclass that holds GCObjectHeader* fields (including
//  via Value members that point to GC objects) MUST override these three
//  methods.  See object.hpp for detailed contracts.
//
//  ArrayObject extends Object with an additional child field:
//    elements : std::vector<Value>  — each Value may hold a GC object.
//
//  Ordering rule:
//    gc_mark       — call base FIRST, then traverse own fields.
//    gc_decref_refs — call base FIRST, then decrement own fields.
//    gc_clear_refs — clear own fields FIRST, then call base (last).

void ArrayObject::gc_mark(std::vector<GCObjectHeader *> &worklist) {
  Object::gc_mark(worklist);
  for (auto &v : elements) {
    if (v.is_object()) {
      auto *obj = v.as<Object>();
      if (obj && !obj->is_marked) {
        obj->is_marked = true;
        worklist.push_back(obj);
      }
    }
  }
}

void ArrayObject::gc_decref_refs() {
  Object::gc_decref_refs();
  for (auto &v : elements) {
    if (v.is_object())
      if (auto *obj = v.as<Object>())
        obj->gc_refs--;
  }
}

void ArrayObject::gc_clear_refs() {
  elements.clear();
  Object::gc_clear_refs();
}

// ─── Factory ─────────────────────────────────────────────────────────────────
//
//  Creates a bare ArrayObject registered with GC, whose __proto__ is
//  Array.prototype (looked up via engine->get_proto(Builtin::array)).
//  Called both from user code (array_constructor) and internally.

Value ArrayObject::create(Engine *e) {
  auto *obj      = new ArrayObject();
  obj->ref_count = 1;
  obj->clsid     = Builtin::array;
  obj->proto     = e->get_proto(Builtin::array);
  e->add_gc_object(obj);
  return Value::object(obj);
}

// ============================================================================
//  ::setup()  —  5‑stage builtin class initialisation
// ============================================================================
//
//  This is the canonical skeleton.  Copy it for every new builtin.
//
//  ┌──────────┬──────────────────────────────────────────────────────┐
//  │  Stage   │  Purpose                                             │
//  ├──────────┼──────────────────────────────────────────────────────┤
//  │ 1. PROTO │ Create the prototype object.  Its __proto__ MUST be  │
//  │          │ the parent class's prototype so that property lookup │
//  │          │ walks up the chain (e.g. arr.hasOwnProperty).        │
//  │          │ The prototype is stored in builtin_protos[id] so     │
//  │          │ that ::create() and Object::create() can find it.    │
//  ├──────────┼──────────────────────────────────────────────────────┤
//  │ 2. METH  │ Set own properties on the prototype for every method │
//  │          │ that instances of this class should inherit.  Use    │
//  │          │ CFunctionObj::create() to wrap C functions.          │
//  │          │ Symbol‑keyed methods (e.g. [Symbol.iterator]) are    │
//  │          │ set with key = engine->known[WellKnown::symbol_*].   │
//  ├──────────┼──────────────────────────────────────────────────────┤
//  │ 3. CTOR  │ Create the constructor function (CFunctionObj).      │
//  │          │ Its `name` property is the class name (e.g. "Array") │
//  │          │ and `length` is the declared parameter count.        │
//  ├──────────┼──────────────────────────────────────────────────────┤
//  │ 4. LINK  │ Bidirectional link:                                  │
//  │          │   ctor["prototype"]   = proto                        │
//  │          │   proto["constructor"] = ctor                        │
//  │          │ This is REQUIRED for `new` to work — CTOR reads      │
//  │          │ ctor["prototype"] to know what __proto__ to use.      │
//  │          │ The constructor back‑link is required by the spec     │
//  │          │ (instances inherit .constructor via the proto chain).│
//  ├──────────┼──────────────────────────────────────────────────────┤
//  │ 5. EXPORT│ Install ctor as a property on the global object so   │
//  │          │ that user code can write `Array` / `new Array(...)`. │
//  └──────────┴──────────────────────────────────────────────────────┘

void ArrayObject::setup(Engine *e) {
  constexpr auto id = Builtin::array;
  auto idx          = static_cast<size_t>(id);

  // ── Stage 1: Prototype ─────────────────────────────────────────────────
  //  __proto__ = Object.prototype  (the parent class's prototype).
  //  Stored in builtin_protos so ArrayObject::create() finds it.
  Value proto = Object::create(e, e->get_proto(Builtin::object), id);
  e->builtin_protos[idx] = proto;
  auto *p                = proto.as<Object>();

  // ── Stage 2: Prototype methods ─────────────────────────────────────────
  //  Symbols use engine->known[] (pre‑interned per‑well‑known atom).
  //  String‑keyed methods use engine->intern().
  auto si_atom = e->known[WellKnown::symbol_iterator];
  p->set_own(e, si_atom, CFunctionObj::create(e, array_values, "[Symbol.iterator]", 0));
  p->set_own(e, e->intern("push"), CFunctionObj::create(e, array_push, "push", 1));

  // ── Stage 3: Constructor function ──────────────────────────────────────
  //  The constructor is itself a callable object (CFunctionObj).
  //  It is created here but linked to the prototype in Stage 4.
  Value ctor = CFunctionObj::create(e, array_constructor, "Array", 1);

  // ── Stage 4: Bidirectional linkage ─────────────────────────────────────
  //  CTOR opcode reads ctor["prototype"] → MUST be set.
  //  Instances read their __proto__["constructor"] to get the class.
  auto proto_atom = e->intern("prototype");
  auto cons_atom  = e->intern("constructor");
  ctor.as<Object>()->set_own(e, proto_atom, proto);
  proto.as<Object>()->set_own(e, cons_atom, ctor);

  // ── Stage 5: Global export ─────────────────────────────────────────────
  e->global_obj.as<Object>()->set_own(e, e->intern("Array"), ctor);
}

} // namespace qjsp
